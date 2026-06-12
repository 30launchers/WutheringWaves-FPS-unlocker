#include <array>
#include <vector>
#include <atomic>
#include <thread>
#include <string>
#include <iostream>

// 防止 windows.h 定义 min 和 max 宏，避免与 std::min/std::max 冲突
#define NOMINMAX
// 减少 Windows.h 包含的内容，加快编译速度
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")

#include "PatternScanner.hpp"
#include "HookUtility.h"

// 加密后的字符串数据
namespace encrypted_strings {
    //WuWa Fps MEM_code
    constexpr auto fps_code = XorString::encrypt("67 00 63 00 2E 00 4C 00 6F 00 77 00 4D 00 65 00 6D 00 6F 00 72 00 79 00 2E 00 49 00 6E 00 63 00 72 00 65 00 6D 00 65 00 6E 00 74 00 61 00 6C 00 47 00 43 00 54 00 69 00 6D 00 65 00 50 00 65 00 72 00 46 00 72 00 61 00 6D 00 65 00 00 00 00 00 4E 00 75 00 6D 00 20 00 74 00 68 00 72 00 65 00 73 00 68 00 6F 00 6C 00 64 00 20 00 66 00 6F 00 72 00 20 00 6C 00 6F 00 77 00 20 00 6F 00 62 00 6A 00 65 00 63 00 74 00 73 00 20 00 47 00 43 00 20 00 6D 00 6F 00 64 00 65 00");
    constexpr auto pipe_code = XorString::encrypt("\\\\.\\pipe\\55984705-F24C-45C2-B2B7-27F047B43A56");
}

// 全局变量，存储从管道接收的数据
//float g_TargetFov = 45;
//int g_EnableAdvan = -1;
//int g_EnableFov = -1;
//int g_EnableHideUID = -1;
//int g_clearcache = -1;
int g_fps = 150;

// 控制台句柄，仅用于本DLL输出
HANDLE g_hConsole = NULL;

// 直接写控制台句柄，不经过stdout（避免显示其他模块的输出）
void Log(const char* fmt, ...)
{
    if (!g_hConsole) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsprintf_s(buf, fmt, args);
    va_end(args);
    strcat_s(buf, "\n");
    DWORD written;
    WriteConsoleA(g_hConsole, buf, (DWORD)strlen(buf), &written, NULL);
}

BOOL __declspec(noinline) OnWinError(const char* szFunction, DWORD dwError)
{
    char szMessage[256];
    wsprintfA(szMessage, "%s failed with error %d", szFunction, dwError);
    MessageBoxA(nullptr, szMessage, "Error", MB_ICONERROR);
    return FALSE;
}

std::atomic<bool> g_IsRunning(true);
// 管道名称定义
//const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\F9FAA61C-A540-15C5-5668-E5C9D66D4AB6";
const char* PIPE_NAME = "";
const int PIPE_BUFFER_SIZE = 4096;

void HandleClient(HANDLE hPipe) {
    char recvbuf[PIPE_BUFFER_SIZE];
    DWORD bytesRead = 0;

    while (g_IsRunning) {
        // 1. 读取数据 (对应 Socket 的 recv)
        // ReadFile 在管道中是阻塞的，如果客户端断开，它会返回 FALSE 或返回 0 字节
        BOOL bSuccess = ReadFile(
            hPipe,
            recvbuf,
            PIPE_BUFFER_SIZE - 1, // 留一位给 '\0'
            &bytesRead,
            NULL
        );

        if (bSuccess && bytesRead > 0) {
            // 安全处理字符串结尾
            recvbuf[bytesRead] = '\0';

            // 解析数据 (保持原样)
            //int itemsMatched = sscanf_s(recvbuf, "%d,%d,%f,%d,%d", &g_EnableAdvan, &g_EnableFov, &g_TargetFov, &g_EnableHideUID, &g_clearcache);
            // 仅解析FPS数据 260611
            int itemsMatched = sscanf_s(recvbuf, "%*d,%*d,%*f,%*d,%*d,%d", &g_fps);

            // (可选) 如果需要回复，可以使用 WriteFile
            // const char* reply = "OK";
            // DWORD written;
            // WriteFile(hPipe, reply, (DWORD)strlen(reply), &written, NULL);
        }
        else {
            // 客户端断开或出错
            // 对于管道，如果 ReadFile 返回 0 或 FALSE，通常意味着连接断开
            if (!bSuccess) {
                int error = GetLastError();
                // ERROR_BROKEN_PIPE (109): 客户端正常关闭
                // ERROR_NO_DATA (232): 客户端关闭了写句柄
                if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA)
                {
                    //DebugPrint("[DLL] Pipe read error: %d\n", error);
                }
            }
            break; // 退出循环
        }
    }

    // 2. 清理连接
    // 管道不需要像 TCP 那样复杂的 shutdown 等待流程
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe); // 断开与客户端的连接，准备下一次连接
    CloseHandle(hPipe);         // 关闭当前句柄
    //DebugPrint("[DLL] Pipe client disconnected.\n");
}

DWORD WINAPI RunNetService(LPVOID lpParam)
{
    auto _pipe_code = XorString::decrypt(encrypted_strings::pipe_code.data(), encrypted_strings::pipe_code.size());
    PIPE_NAME = _pipe_code.c_str();

    // 循环处理连接
    while (g_IsRunning)
    {
        //HANDLE hPipe = CreateNamedPipe(
        //    PIPE_NAME,
        //    PIPE_ACCESS_DUPLEX,       // 双向
        //    PIPE_TYPE_MESSAGE |       // 消息流模式 (类似 TCP 的消息边界)
        //    PIPE_READMODE_MESSAGE |
        //    PIPE_WAIT,
        //    PIPE_UNLIMITED_INSTANCES, // 最大实例数
        //    PIPE_BUFFER_SIZE,         // 输出缓冲
        //    PIPE_BUFFER_SIZE,         // 输入缓冲
        //    0,                        // 默认超时
        //    NULL                      // 默认安全
        //);


        // 窄字符版，使用CreateNamedPipeA而不是CreateNamedPipe
        HANDLE hPipe = CreateNamedPipeA(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,       // 双向
            PIPE_TYPE_MESSAGE |       // 消息流模式 (类似 TCP 的消息边界)
            PIPE_READMODE_MESSAGE |
            PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, // 最大实例数
            PIPE_BUFFER_SIZE,         // 输出缓冲
            PIPE_BUFFER_SIZE,         // 输入缓冲
            0,                        // 默认超时
            NULL                      // 默认安全
        );


        if (hPipe == INVALID_HANDLE_VALUE) {
            OnWinError("CreateNamedPipe", GetLastError());
            Sleep(1000); // 出错等待一下防止死循环刷屏
            continue;
        }

        BOOL bConnected = ConnectNamedPipe(hPipe, NULL);

        // 如果 ConnectNamedPipe 返回 0，检查是否是因为客户端已连接
        if (!bConnected && GetLastError() != ERROR_PIPE_CONNECTED) {
            // 连接失败，关闭句柄重试
            CloseHandle(hPipe);
            continue;
        }

        HandleClient(hPipe);
    }

    //DebugPrint("[DLL] Pipe service stopped.\n");
    return 0;
}


// 安全写入 float
static BOOL SafeWriteFloat(uintptr_t address, float value) {
    __try {
        *(float*)address = value;
        return TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

// 辅助函数：安全读取字节（隔离SEH，避免与C++对象展开冲突）
static uint8_t SafeReadByte(uint64_t addr)
{
    __try
    {
        return *(uint8_t*)addr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0xFF; // 返回特殊值表示读取失败
    }
}

// 辅助函数：安全读取float（隔离SEH，避免与C++对象展开冲突）
static bool SafeReadFloat(uint64_t addr, float* outValue)
{
    __try
    {
        *outValue = *(float*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


void WriteLoop(uint64_t addr)
{
    while (true)
    {
        float fpsvalue = 150.0f;
        fpsvalue = (float)g_fps; // 从全局变量获取当前FPS值

        if (!SafeWriteFloat(addr, fpsvalue))
        {
            //Log("写入失败: 0x%llX", addr);
            break;
        }

        Sleep(51); // 每51ms写一次
    }
}


// 从特征码地址搜索FPS float值（移植自MainWindow.xaml.cs）
void SearchFPSAddress(uint64_t baseAddress)
{
    uint64_t addr = baseAddress;
    uint64_t foundAddress = 0;

    while (addr < 0x7FFFFFFFFFFFULL)
    {
        uint8_t value = SafeReadByte(addr);

        if (value == 0x70)
        {
            foundAddress = addr;
            //Log("找到特征码'70'的位置: 0x%llX", addr);
            break;
        }

        if (value == 0xFF)
        {
            //Log("读取内存失败!");
            break;
        }

        addr++;
    }

    if (foundAddress != 0)
    {
        // 在这里处理找到的地址
    }
    else
    {
        MessageBoxA(NULL, "Could not find fps pattern2!", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // 保存找到的 float 地址
    std::vector<uint64_t> foundAddresses;

    // 限定搜索范围
    uint64_t rangeStart = addr;
    uint64_t rangeEnd = (rangeStart > 0x1000) ? (rangeStart - 0x1000) : 0;

    // 超时设置：915 秒
    auto startTime = std::chrono::steady_clock::now();
    const std::chrono::seconds timeout(915);
    bool timedOut = false;

    // 来回搜索（与MainWindow.xaml.cs逻辑一致）
    while (addr > rangeEnd && addr > 0 && !timedOut)
    {
        float value = 0;
        bool ok = SafeReadFloat(addr, &value);

        if (ok)
        {
            if (value == 30.0f || value == 45.0f || value == 60.0f || value == 120.0f)
            {
                foundAddresses.push_back(addr);
                //Log("找到FPS值 %.1f 地址: 0x%llX", value, addr);

                if (foundAddresses.size() == 2)
                {
                    break;
                }
            }
        }
        else
        {
            //Log("读取内存失败!");
            break;
        }

        addr -= 1;

        // 如果超出了范围则从起始位置开始搜索
        if (addr <= rangeEnd)
        {
            //Log("已到达范围底部，重新向上搜索...");
            addr = rangeStart;
            rangeEnd = rangeStart - 0x1000;
        }

        // 超时检查
        if (std::chrono::steady_clock::now() - startTime >= timeout)
        {
            timedOut = true;
        }
    }

    if (foundAddresses.size() >= 2)
    {
        //Log("第二个找到的地址: 0x%llX", foundAddresses[1]);
        //Log("成功找到第二个目标地址，开始写入循环...");
        WriteLoop(foundAddresses[1]);
    }
    else
    {
        //MessageBoxA(NULL, "Could not find FPS value.", "Error", MB_OK | MB_ICONERROR);
        MessageBoxA(NULL, "Scan timed out.\n\nCould not find FPS value.", "Error", MB_OK | MB_ICONERROR);
    }
}


void RunLogic()
{
    ////分配控制台窗口
    //AllocConsole();
    //g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    //Log("Console Initialized");

    constexpr DWORD WAIT_TIME_MS = 15 * 60 * 1000; // 15分钟

    if (TestUnRealWin::WaitForUnityWindow(WAIT_TIME_MS))
    {
        Sleep(50);
    }

    HMODULE hWuWa = GetModuleHandleA(nullptr);
    if (!hWuWa)
    {
        return;
    }

    auto _fps_code = XorString::decrypt(encrypted_strings::fps_code.data(), encrypted_strings::fps_code.size());
    auto fps_scan_results = PatternScanner::MultipleScan(_fps_code.c_str());

    // 1. 检查结果列表是否为空，防止越界访问
    if (!fps_scan_results.empty())
    {
        // 2. 取出第一个结果
        uint64_t firstAddress = fps_scan_results[0];

        // 打印日志确认
        //Log("获取到首个特征码地址: 0x%llx", firstAddress);

        // 3. 搜索FPS float值地址（移植自MainWindow.xaml.cs）
        SearchFPSAddress(firstAddress);
    }
    else
    {
        //Log("未扫描到特征码！");
        MessageBoxA(NULL,
            "Could not find fps pattern\r\n"
            "The reason might be:\r\n"
            "1. Wuthering Waves was not closed correctly\r\n"
            "2. The user selected the wrong Wuthering Waves EXE file\r\n"
            "3. Game version update causes memory address changes that cannot be modified", "Notification",
            MB_OK | MB_ICONERROR);
    }

    // 测试完成后显示成功消息框（可选）
    //MessageBoxA(NULL, "DLL Inject Success!", "Information", MB_OK | MB_ICONINFORMATION);
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved)
{
    if (hInstance)
        DisableThreadLibraryCalls(hInstance);

    // 检查是否是目标进程 260202
    HMODULE hStarRail = GetModuleHandleA("StarRail.exe");
    HMODULE hGenshinImpact = GetModuleHandleA("GenshinImpact.exe");
    HMODULE hWuWa = GetModuleHandleA("Client-Win64-Shipping.exe");

    // 如果不是目标进程，直接返回TRUE（DLL加载成功但不初始化）
    if (!hWuWa && !hGenshinImpact && !hStarRail) {
        return TRUE;
    }

    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        // 260202 启动逻辑线程更改
        LPTHREAD_START_ROUTINE startRoutine = nullptr;

        // 判断当前是哪个进程
        if (hStarRail || hGenshinImpact) {
            // sr或genshin进程，执行genshin逻辑
            //startRoutine = (LPTHREAD_START_ROUTINE)RunLogicGenshin;
        }
        else if (hWuWa) {
            // sr进程，执行sr逻辑
            startRoutine = (LPTHREAD_START_ROUTINE)RunLogic;
        }

        if (startRoutine) {
            const auto hThread = CreateThread(nullptr, 0, startRoutine, nullptr, 0, nullptr);
            if (!hThread)
                return OnWinError("CreateThread", GetLastError());

            CloseHandle(hThread);

            // 启动网络服务线程
            const auto hThreadNet = CreateThread(nullptr, 0, RunNetService, nullptr, 0, nullptr);
            if (!hThreadNet)
            {
                return OnWinError("CreateThreadNet", GetLastError());
            }
            CloseHandle(hThreadNet);
        }
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        // 禁用所有钩子
        //MinHookManager::DisableAllHooks();
    }

    return TRUE;

}