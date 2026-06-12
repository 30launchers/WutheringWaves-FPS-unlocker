#pragma once
#include <string>

// XOR加密解密函数
class XorString {
private:
    static constexpr char key = 0xA17FC; // 加密密钥

public:
    // 编译时加密
    template<size_t N>
    static constexpr auto encrypt(const char(&str)[N]) {
        std::array<char, N> encrypted{};
        for (size_t i = 0; i < N; ++i) {
            encrypted[i] = str[i] ^ key;
        }
        return encrypted;
    }

    //// 运行时解密
    //static std::string decrypt(const char* encrypted, size_t len) {
    //    std::string deccrypted;
    //    deccrypted.reserve(len);
    //    for (size_t i = 0; i < len; ++i) {
    //        deccrypted += encrypted[i] ^ key;
    //    }
    //    return deccrypted;
    //}

    //// 辅助函数：向调试器输出格式化字符串
    //void DebugPrintDecry(const char* format, ...) {
    //    // 解密格式字符串
    //    auto decrypted_format = XorString::decrypt(format, strlen(format));

    //    char buffer[1024];
    //    va_list args;
    //    va_start(args, format);
    //    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, decrypted_format.c_str(), args);
    //    va_end(args);
    //    OutputDebugStringA(buffer);
    //}

    // 运行时解密
    static std::string decrypt(const char* encrypted, size_t len) {
        std::string decrypted;
        decrypted.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            char c = encrypted[i] ^ key;
            // 如果遇到加密后的空终止符，停止解密
            if (c == '\0') break;
            decrypted += c;
        }
        return decrypted;
    }
};

// 辅助函数：向调试器输出格式化字符串
void DebugPrint(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}


class TestUnRealWin
{
private:
    // 枚举窗口的回调数据
    struct EnumWindowsData {
        DWORD processId;
        HWND foundWindow;
    };

public:
    // 枚举回调函数
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
        EnumWindowsData* data = (EnumWindowsData*)lParam;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);

        if (pid == data->processId && IsWindowVisible(hwnd)) {
            if (GetWindowTextLengthA(hwnd) > 0) {
                char className[256];
                GetClassNameA(hwnd, className, sizeof(className));

                if (strcmp(className, "UnrealWindow") == 0) {
                    data->foundWindow = hwnd;
                    return FALSE; // 停止枚举
                }
            }
        }
        return TRUE;
    }

    // 检查Unity主窗口是否存在（单次检查）
    static bool IsUnityWindowReady() {
        EnumWindowsData data;
        data.processId = GetCurrentProcessId();
        data.foundWindow = NULL;

        EnumWindows(EnumWindowsProc, (LPARAM)&data);

        return (data.foundWindow != NULL);
    }

    // 循环等待Unity主窗口出现
    static bool WaitForUnityWindow(DWORD timeoutMs = 30000) {
        DWORD startTime = GetTickCount();

        while (GetTickCount() - startTime < timeoutMs) {
            if (IsUnityWindowReady()) {
                return true; // 找到了
            }
            Sleep(2);
        }

        return false; // 超时
    }
};
