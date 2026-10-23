#pragma once
//原理方法 https://space.bilibili.com/39817183
#include <windows.h>
#include <atomic>
#include <unordered_map>

namespace MobileUI
{
    namespace detail
    {
        // 与原版对应的窗口属性名
        static const char* kTabletProp = "MicrosoftTabletPenServiceProperty";

        // 移植 WindowSetting
        struct WindowSetting
        {
            DWORD pid;
            HANDLE original;   // GetProp 取到的原始值（原版 IntPtr）
            HANDLE applied;    // original | 1 后写入的值
        };

        // 移植 windows / touchActive / hook / FilterRules.blockedButtons
        struct TouchState
        {
            std::atomic<bool> active{ false };
            HHOOK hook = nullptr;
            DWORD hookThreadId = 0;
            int blockedButtons = 0;
            std::unordered_map<HWND, WindowSetting> windows;
        };

        inline TouchState& State()
        {
            static TouchState s;
            return s;
        }

        // 返回 true = 屏蔽该鼠标消息
        static bool BlockMessage(int message, bool gameForeground)
        {
            // 按钮位映射：
            // 513-515 左键=1，516-518 右键=2，519-521 中键=4，523-525 X键=8，其余=0
            int bit;
            if (message >= 513 && message <= 515)
                bit = 1;
            else if (message >= 516 && message <= 518)
                bit = 2;
            else if (message >= 519 && message <= 521)
                bit = 4;
            else if (message >= 523 && message <= 525)
                bit = 8;
            else
                bit = 0;

            // 抬起消息（514/517/520/524）：游戏前台，或对应的按下曾被屏蔽过 -> 屏蔽
            if (message == 514 || message == 517 || message == 520 || message == 524)
            {
                bool block = gameForeground || (State().blockedButtons & bit) != 0;
                State().blockedButtons &= ~bit;
                return block;
            }

            // 其余鼠标消息（512-526 范围内）：仅游戏前台时屏蔽并记录按钮位
            if (!gameForeground || message < 512 || message > 526)
                return false;

            State().blockedButtons |= bit;
            return true;
        }

        // ---- 移植 OnMouse（WH_MOUSE_LL 回调）----
        static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
        {
            if (nCode >= 0)
            {
                DWORD pid = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &pid);
                // 原版：前台窗口 PID 在 gameIds（游戏进程集合）中即视为游戏前台；
                // 进程内移植：直接与本进程 PID 比较
                bool gameForeground = (pid == GetCurrentProcessId());
                if (BlockMessage(static_cast<int>(wParam), gameForeground))
                    return 1; // 屏蔽该消息
            }
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        // ---- 移植 ConfigureGameWindows 的枚举回调 ----
        static BOOL CALLBACK ConfigEnumProc(HWND hwnd, LPARAM lParam)
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId()) // 原版过滤 gameIds，进程内即本进程
                return TRUE;

            TouchState& st = State();
            WindowSetting setting;
            auto it = st.windows.find(hwnd);
            if (it == st.windows.end() || it->second.pid != pid)
            {
                setting.pid = pid;
                setting.original = GetPropA(hwnd, kTabletProp);
            }
            else
            {
                setting = it->second;
            }

            // 关键：原始值 | 1，使窗口接受触摸/指针输入
            setting.applied = reinterpret_cast<HANDLE>(
                reinterpret_cast<ULONG_PTR>(setting.original) | 1);

            if (SetPropA(hwnd, kTabletProp, setting.applied))
                st.windows[hwnd] = setting;
            return TRUE;
        }

        // ---- 移植 ConfigureGameWindows ----
        static void ConfigureGameWindows()
        {
            EnumWindows(ConfigEnumProc, 0);
        }

        // ---- 移植 RestoreGameWindows ----
        static void RestoreGameWindows()
        {
            TouchState& st = State();
            for (const auto& kv : st.windows)
            {
                HWND hwnd = kv.first;
                const WindowSetting& setting = kv.second;

                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == setting.pid && GetPropA(hwnd, kTabletProp) == setting.applied)
                {
                    if (setting.original == nullptr)
                        RemovePropA(hwnd, kTabletProp);
                    else
                        SetPropA(hwnd, kTabletProp, setting.original);
                }
            }
        }

        // ---- 工作线程：装钩子 + 轮询应用窗口属性 + 消息泵 ----
        static DWORD WINAPI TouchWorker(LPVOID lpParam)
        {
            (void)lpParam;
            TouchState& st = State();
            st.hookThreadId = GetCurrentThreadId();

            // 立即安装低级鼠标钩子（窗口尚未创建前钩子不会屏蔽任何消息，
            // 因为前台 PID 不匹配；对应原版 EnableTouch 中 SetWindowsHookEx）
            st.hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc,
                GetModuleHandleW(nullptr), 0);

            constexpr ULONGLONG APPLY_INTERVAL_MS = 1000; // 周期性重刷属性（相当于持续 RefreshTouch）
            ULONGLONG nextApply = 0; // 0 = 立即应用一次
            bool quit = false;

            while (st.active.load())
            {
                ULONGLONG now = GetTickCount64();
                if (nextApply == 0 || now >= nextApply)
                {
                    ConfigureGameWindows();
                    nextApply = now + APPLY_INTERVAL_MS;
                }

                ULONGLONG remaining = nextApply > now ? (nextApply - now) : 1;

                // 等待新消息或超时；低级钩子回调以消息形式投递到本线程，
                // 有鼠标事件时会立刻唤醒，随后由 PeekMessage 泵出触发回调
                DWORD wait = MsgWaitForMultipleObjectsEx(
                    0, nullptr, static_cast<DWORD>(remaining),
                    QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (wait == WAIT_FAILED)
                    break;

                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                    {
                        quit = true;
                        break;
                    }
                    DispatchMessageW(&msg);
                }
                if (quit)
                    break;
            }

            // ---- 对应 DisableTouch 的清理部分 ----
            if (st.hook)
            {
                UnhookWindowsHookEx(st.hook);
                st.hook = nullptr;
            }
            RestoreGameWindows();
            st.windows.clear();
            st.blockedButtons = 0;
            st.hookThreadId = 0;
            return 0;
        }
    }

    // 由 dllmain 的 LoadConfig 在 UseMobileUI=1 时调用（可重复调用，内部有防重入）
    static void Start()
    {
        detail::TouchState& st = detail::State();
        bool expected = false;
        if (!st.active.compare_exchange_strong(expected, true)) // 已启用则直接返回
            return;

        HANDLE hThread = CreateThread(nullptr, 0, detail::TouchWorker, nullptr, 0, nullptr);
        if (hThread)
            CloseHandle(hThread);
        else
            st.active = false;
    }

    // 卸载钩子并还原窗口属性（通知工作线程自行清理，避免跨线程竞争）
    static void Stop()
    {
        detail::TouchState& st = detail::State();
        if (!st.active.exchange(false))
            return;

        DWORD tid = st.hookThreadId;
        if (tid)
            PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }
}
