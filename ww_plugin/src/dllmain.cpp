#include <windows.h>
#include <thread>
#include <chrono>
#include <string>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <vector>
#include <atomic>
#include <array>

#include "PatternScanner.hpp"
#include "HookUtility.h"

#include <SDK.hpp>

using namespace SDK;

// 加密后的字符串数据
namespace encrypted_strings {
    constexpr auto gobjects_code = XorString::encrypt("00 00 21 00 ?? ?? 00 00 21");
    constexpr auto appendstring_code = XorString::encrypt("48 89 5C 24 20 56 48 83 EC 20 80 3D ?? ?? ?? ?? 00 48");
    constexpr auto pipe_code = XorString::encrypt("\\\\.\\pipe\\F9FAA61C-A540-15C5-5668-E5C9D66D4AB6");
}


extern "C" {
    bool SEH_GetDisplayName(UUIItem* uiItem, wchar_t* outBuffer, int bufferSize);
    bool SEH_GetActors(ULevel* level, AActor*** outActors, int* outCount);
    bool SEH_GetChildren(UUIItem* uiItem, UUIItem*** outChildren, int* outCount);
    bool SEH_GetWorldLevels(UWorld* world, ULevel*** outLevels, int* outCount, ULevel** outPersistentLevel, bool* outHasPersistent);
    void SEH_ProcessActor(AActor* actor,
        bool debugMode,
        int* totalDumpedCount,
        UUIItem** foundUIDWidgets,
        int* foundCount,
        int MAX_FOUND,
        bool (*IsUIDWidgetName)(const wchar_t*));
}

//constexpr float TARGET_FOV = 150.0f;
constexpr SIZE_T DUMP_RANGE = 128;


float g_TargetFov = 45;
int g_EnableAdvan = -1;
int g_EnableFov = -1;
int g_EnableHideUID = -1;
int g_clearcache = -1;

UUIItem** foundUIDWidgets = nullptr;
int foundCount = 0;
const int MAX_FOUND = 512;
bool uidHidden = false;
int lastShouldHide = -1;
int lastEnableAdvan = -1;
static UUIItem* widgetBuffer[MAX_FOUND] = {};

static UUIItem* cachedUIDWidgets[MAX_FOUND] = {};
static int cachedCount = 0;
static bool cacheValid = false;

int totalDumpedCount = 0;
bool debugMode = false;

std::mutex uidMutex;
std::atomic<bool> searching(false);
std::atomic<bool> searchComplete(false);
std::atomic<int> activeThreads(0);
const int THREAD_COUNT = 30;



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
            int itemsMatched = sscanf_s(recvbuf, "%d,%d,%f,%d,%d", &g_EnableAdvan, &g_EnableFov, &g_TargetFov, &g_EnableHideUID, &g_clearcache);

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

std::string GetDllDirectory(HMODULE hModule) {
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(hModule, path, MAX_PATH);
    std::string::size_type pos = std::string(path).find_last_of("\\/");
    return std::string(path).substr(0, pos);
}

void LogToFile(const std::string& logPath, const char* format, ...) {
    FILE* fp = fopen(logPath.c_str(), "a");
    if (fp) {
        time_t now = time(0);
        struct tm tstruct;
        localtime_s(&tstruct, &now);
        char timeBuf[80];
        strftime(timeBuf, sizeof(timeBuf), "[%Y-%m-%d %H:%M:%S] ", &tstruct);
        fprintf(fp, "%s", timeBuf);
        va_list args;
        va_start(args, format);
        vfprintf(fp, format, args);
        va_end(args);
        fprintf(fp, "\n");
        fclose(fp);
    }
}

void DumpMemoryToFile(const std::string& logPath, uintptr_t targetAddr,
    uintptr_t baseAddress, const char* tag) {
    FILE* fp = fopen(logPath.c_str(), "a");
    if (!fp) return;

    uintptr_t startAddr = targetAddr - DUMP_RANGE;
    SIZE_T totalSize = DUMP_RANGE * 2;
    BYTE* buffer = (BYTE*)malloc(totalSize);
    if (buffer == NULL) { fclose(fp); return; }

    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)startAddr, buffer, totalSize, &bytesRead)) {
        fprintf(fp, "[%s] 读取内存失败 (错误码: %d)\n", tag, GetLastError());
        free(buffer); fclose(fp); return;
    }

    uintptr_t startOffset = startAddr - baseAddress;
    uintptr_t endOffset = startAddr + bytesRead - 1 - baseAddress;

    fprintf(fp, "---------- [%s] 内存快照 ----------\n", tag);
    fprintf(fp, "绝对地址: 0x%p ~ 0x%p\n", (LPVOID)startAddr, (LPVOID)(startAddr + bytesRead - 1));
    fprintf(fp, "相对偏移: EXE+0x%llX ~ EXE+0x%llX\n", (unsigned long long)startOffset, (unsigned long long)endOffset);
    fprintf(fp, "------------------------------------------------------------\n");

    for (SIZE_T i = 0; i < bytesRead; i += 16) {
        fprintf(fp, "%08llX: ", (unsigned long long)(startAddr + i));
        for (SIZE_T j = 0; j < 16; j++) {
            if (i + j < bytesRead) fprintf(fp, "%02X ", buffer[i + j]);
            else fprintf(fp, "   ");
            if (j == 7) fprintf(fp, " ");
        }
        fprintf(fp, " ");
        for (SIZE_T j = 0; j < 16; j++) {
            if (i + j < bytesRead) {
                char c = buffer[i + j];
                fprintf(fp, "%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "------------------------------------------------------------\n");
    free(buffer);
    fclose(fp);
}

void DumpConfigLog(HMODULE hModule) {
    std::string dllDir = GetDllDirectory(hModule);
    std::string logPath = dllDir + "\\configdump.log";

    HMODULE hExe = GetModuleHandleA(NULL);
    uintptr_t baseAddress = (uintptr_t)hExe;

    LogToFile(logPath, "Config Address Dump");
    LogToFile(logPath, "EXE Base      = 0x%p", (LPVOID)baseAddress);
    LogToFile(logPath, "GObjects      = 0x%08X (EXE+0x%08X)", SDK::Offsets::GObjects, SDK::Offsets::GObjects);
    LogToFile(logPath, "AppendString  = 0x%08X (EXE+0x%08X)", SDK::Offsets::AppendString, SDK::Offsets::AppendString);

    DumpMemoryToFile(logPath, baseAddress + SDK::Offsets::GObjects, baseAddress, "GObjects");
    DumpMemoryToFile(logPath, baseAddress + SDK::Offsets::AppendString, baseAddress, "AppendString");
}

bool IsPointerReadable(const void* ptr) {
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return false;
    constexpr DWORD readable_flags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
        PAGE_EXECUTE_WRITECOPY;
    return (mbi.State == MEM_COMMIT) && (mbi.Protect & readable_flags) && !(mbi.Protect & PAGE_GUARD);
}

UWorld* GetWorld() {
    __try {
        UWorld* world = UWorld::GetWorld();
        if (world && IsPointerReadable(world)) {
            return world;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

APlayerController* GetPlayerController() {
    UWorld* world = GetWorld();
    if (!world || !IsPointerReadable(world)) return nullptr;

    __try {
        if (!IsPointerReadable(&world->OwningGameInstance)) return nullptr;

        UGameInstance* gameInstance = world->OwningGameInstance;
        if (!gameInstance || !IsPointerReadable(gameInstance)) return nullptr;

        if (!IsPointerReadable(&gameInstance->LocalPlayers)) return nullptr;

        TArray<ULocalPlayer*> localPlayers = gameInstance->LocalPlayers;
        if (localPlayers.Num() < 1) return nullptr;

        ULocalPlayer* localPlayer = localPlayers[0];
        if (!localPlayer || !IsPointerReadable(localPlayer)) return nullptr;

        if (!IsPointerReadable(&localPlayer->PlayerController)) return nullptr;

        APlayerController* playerController = localPlayer->PlayerController;
        if (playerController && IsPointerReadable(playerController)) {
            return playerController;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }

    return nullptr;
}

void ScanAndInitOffsets() {

    auto _gobjects_code = XorString::decrypt(encrypted_strings::gobjects_code.data(), encrypted_strings::gobjects_code.size());
    auto _appendstring_code = XorString::decrypt(encrypted_strings::appendstring_code.data(), encrypted_strings::appendstring_code.size());

    //const char* pattern1 = "00 00 21 00 ?? ?? 00 00 21";
    //const char* pattern2 = "48 89 5C 24 20 56 48 83 EC 20 80 3D ?? ?? ?? ?? 00 48";
    const wchar_t* moduleName = nullptr;

    HMODULE hExe = GetModuleHandleA(NULL);
    uintptr_t baseAddress = (uintptr_t)hExe;

    const auto timeout_duration = std::chrono::minutes(2);
    // 记录开始时间
    auto start_time = std::chrono::steady_clock::now();

    // ==================== 扫描 Pattern 1 ====================
    bool pattern1_found = false;
    while (true) {
        //auto results1 = PatternScanner::MultipleScanModule(pattern1, moduleName);
        auto results1 = PatternScanner::MultipleScanModule(_gobjects_code.c_str(), moduleName);

        if (!results1.empty() && results1[0] >= 0x10) {
            uintptr_t gObjectsAddr = results1[0] - 0x10;
            printf("[+] Pattern1 Found: %016llX\n", (unsigned long long)gObjectsAddr);

            // 初始化GObjects给SDK
            SDK::Offsets::SetGObjects(static_cast<int32>(gObjectsAddr - baseAddress));
            pattern1_found = true;
            break; // 找到了，跳出循环
        }

        // 检查是否超时
        auto current_time = std::chrono::steady_clock::now();
        if (current_time - start_time >= timeout_duration) {
            MessageBoxA(NULL, "Pattern1 scan timed out (not found within 2 minutes)!", "Error", MB_OK | MB_ICONERROR);
            break; // 超时，跳出循环
        }

        // 没找到且没超时，稍微休眠避免CPU占用过高（例如每500ms扫描一次）
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // ==================== 扫描 Pattern 2 ====================
    // 注意：这里选择无论Pattern 1是否成功，都继续尝试扫描Pattern 2。
    // 如果你的逻辑是 Pattern1 失败就不扫描 Pattern2 了，可以加上 if (pattern1_found) { ... }
    bool pattern2_found = false;
    while (true) {
        //auto results2 = PatternScanner::MultipleScanModule(pattern2, moduleName);
        auto results2 = PatternScanner::MultipleScanModule(_appendstring_code.c_str(), moduleName);

        if (!results2.empty()) {
            uintptr_t appendStringAddr = results2[0];
            printf("[+] Pattern2 Found: %016llX\n", (unsigned long long)appendStringAddr);

            // 初始化AppendString给SDK
            SDK::Offsets::SetAppendString(static_cast<int32>(appendStringAddr - baseAddress));
            pattern2_found = true;
            break; // 找到了，跳出循环
        }

        // 检查是否超时
        auto current_time = std::chrono::steady_clock::now();
        if (current_time - start_time >= timeout_duration) {
            MessageBoxA(NULL, "Pattern2 scan timed out (not found within 2 minutes)!", "Error", MB_OK | MB_ICONERROR);
            break; // 超时，跳出循环
        }

        // 没找到且没超时，稍微休眠避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

bool IsUIDWidgetName(const wchar_t* name) {
    if (!name) return false;
    if (wcscmp(name, L"UiView_UID_Prefab") == 0) return true;
    return false;
}

void ThreadWorker(int threadId, int startIdx, int endIdx, AActor** actors, int totalCount) {
    activeThreads++;

    for (int i = startIdx; i < endIdx && i < totalCount; i++) {
        if (searchComplete.load()) break;

        AActor* actor = actors[i];
        if (!actor) continue;

        std::lock_guard<std::mutex> lock(uidMutex);
        SEH_ProcessActor(actor, debugMode, &totalDumpedCount, foundUIDWidgets, &foundCount, MAX_FOUND, IsUIDWidgetName);
    }

    activeThreads--;

    char threadBuf[128];
    sprintf_s(threadBuf, "[Thread %d] Finished, range: %d-%d\n", threadId, startIdx, endIdx);
    OutputDebugStringA(threadBuf);
}

void MultiThreadFindUID(ULevel* level) {
    AActor** actors = nullptr;
    int actorCount = 0;

    if (!SEH_GetActors(level, &actors, &actorCount) || !actors) return;

    char levelBuf[128];
    sprintf_s(levelBuf, "[Level] Actors count: %d, Starting %d-thread search...\n", actorCount, THREAD_COUNT);
    OutputDebugStringA(levelBuf);

    if (actorCount == 0) {
        free(actors);
        return;
    }

    int actorsPerThread = (actorCount + THREAD_COUNT - 1) / THREAD_COUNT;
    std::vector<std::thread> threads;

    for (int t = 0; t < THREAD_COUNT; t++) {
        int startIdx = t * actorsPerThread;
        int endIdx = startIdx + actorsPerThread;

        if (startIdx >= actorCount) break;

        threads.emplace_back(ThreadWorker, t, startIdx, endIdx, actors, actorCount);
    }

    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }

    free(actors);
    OutputDebugStringA("[Search] All threads completed\n");
}

void HideUID() {
    OutputDebugStringA("\n========== Starting UI Hide ==========\n");

    if (cacheValid && cachedCount > 0) {
        foundUIDWidgets = cachedUIDWidgets;
        foundCount = cachedCount;

        char cacheBuf[256];
        sprintf_s(cacheBuf, "[Cache] Using cached widgets: %d\n", cachedCount);
        OutputDebugStringA(cacheBuf);
    }
    else {
        foundUIDWidgets = widgetBuffer;
        foundCount = 0;
        totalDumpedCount = 0;

        OutputDebugStringA("[Search] Performing multi-threaded UI search...\n");

        UWorld* world = GetWorld();
        if (!world) {
            OutputDebugStringA("[Error] World is null!\n");
            return;
        }

        ULevel** levels = nullptr;
        int levelCount = 0;
        ULevel* persistentLevel = nullptr;
        bool hasPersistent = false;

        if (!SEH_GetWorldLevels(world, &levels, &levelCount, &persistentLevel, &hasPersistent)) {
            OutputDebugStringA("[Error] Cannot get level!\n");
            return;
        }

        char buf[256];
        if (hasPersistent && persistentLevel) {
            sprintf_s(buf, "[World] Using PersistentLevel (multi-thread mode)\n");
            OutputDebugStringA(buf);
            MultiThreadFindUID(persistentLevel);
        }
        else if (levels && levelCount > 0) {
            OutputDebugStringA("[Warning] PersistentLevel is null, fallback to all Levels\n");
            sprintf_s(buf, "[World] Levels count: %d\n", levelCount);
            OutputDebugStringA(buf);
            for (int i = 0; i < levelCount; i++) {
                MultiThreadFindUID(levels[i]);
            }
        }
        else {
            OutputDebugStringA("[Error] No levels available!\n");
            if (levels) free(levels);
            return;
        }

        if (levels) free(levels);

        char msg[256];
        sprintf_s(msg, "========== Total dumped widgets: %d, UID matched: %d ==========\n\n", totalDumpedCount, foundCount);
        OutputDebugStringA(msg);

        memcpy(cachedUIDWidgets, widgetBuffer, sizeof(UUIItem*) * foundCount);
        cachedCount = foundCount;
        cacheValid = true;
    }

    if (foundCount > 0) {
        uidHidden = true;
        OutputDebugStringA("[Success] UID hidden!\n");

        for (int i = 0; i < foundCount; i++) {
            UUIItem* widget = foundUIDWidgets[i];
            if (widget) {
                __try {
                    widget->SetIsUIActive(false);
                    widget->SetAlpha(0.0f);
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }
    else {
        OutputDebugStringA("[Fail] No UID widgets matched!\n");
    }
}

void ShowUID() {
    if (cacheValid && cachedCount > 0) {
        foundUIDWidgets = cachedUIDWidgets;
        foundCount = cachedCount;
    }

    if (foundUIDWidgets && foundCount > 0) {
        for (int i = 0; i < foundCount; i++) {
            UUIItem* widget = foundUIDWidgets[i];
            if (widget) {
                __try {
                    widget->SetIsUIActive(true);
                    widget->SetAlpha(1.0f);

                    __try {
                        bool* bIsActive = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(widget) + 0x4C7);
                        *bIsActive = true;

                        float* alpha = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(widget) + 0x378);
                        *alpha = 1.0f;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }
    uidHidden = false;
    OutputDebugStringA("[Info] UID shown!\n");
}

void BackgroundSearchLoop() {
    searching.store(true);
    searchComplete.store(false);

    OutputDebugStringA("[Background] Starting continuous UID search with 30 threads...\n");

    while (!searchComplete.load() && !uidHidden) {
        if (g_EnableHideUID == 1) {
            HideUID();
        }
        else {
            UWorld* world = GetWorld();
            if (world) {
                ULevel** levels = nullptr;
                int levelCount = 0;
                ULevel* persistentLevel = nullptr;
                bool hasPersistent = false;

                if (SEH_GetWorldLevels(world, &levels, &levelCount, &persistentLevel, &hasPersistent)) {
                    if (hasPersistent && persistentLevel) {
                        MultiThreadFindUID(persistentLevel);
                    }
                    else if (levels && levelCount > 0) {
                        for (int i = 0; i < levelCount; i++) {
                            MultiThreadFindUID(levels[i]);
                        }
                    }
                    if (levels) free(levels);

                    if (foundCount > 0) {
                        memcpy(cachedUIDWidgets, widgetBuffer, sizeof(UUIItem*) * foundCount);
                        cachedCount = foundCount;
                        cacheValid = true;
                        searchComplete.store(true);
                        OutputDebugStringA("[Background] UID widgets cached (not hidden)\n");
                        break;
                    }
                }
            }
        }

        if (foundCount > 0 && g_EnableHideUID == 1) {
            searchComplete.store(true);
            OutputDebugStringA("[Background] UID found and hidden!\n");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    searching.store(false);
}

void ClearUIDCache() {
    std::lock_guard<std::mutex> lock(uidMutex);  // 线程安全

    // 1. 清空缓存数组
    memset(cachedUIDWidgets, 0, sizeof(cachedUIDWidgets));

    // 2. 重置缓存状态
    cachedCount = 0;
    cacheValid = false;

    // 3. 重置当前查找结果
    foundUIDWidgets = nullptr;
    foundCount = 0;

    // 4. 允许重新搜索
    searchComplete.store(false);

    OutputDebugStringA("[Info] UID cache cleared!\n");
}

DWORD WINAPI MainThreadUID(HMODULE hModule) {
    while (!FindWindowA("UnrealWindow", nullptr)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    OutputDebugStringA("\n========================================\n");
    OutputDebugStringA("[wwuid] DLL loaded\n");
    OutputDebugStringA("[wwuid] Auto-searching UID with 30 threads...\n");
    OutputDebugStringA("Press F6 to hide/show UID\n");
    OutputDebugStringA("Press F7 to dump UI widgets (debug)\n");
    OutputDebugStringA("Press END to exit\n");
    OutputDebugStringA("========================================\n\n");

    std::thread searchThread(BackgroundSearchLoop);
    searchThread.detach();

    while (true) {
        //if (GetAsyncKeyState(VK_END) & 0x8000) {
        //    searchComplete.store(true);
        //    ShowUID();
        //    break;
        //}

        //if (GetAsyncKeyState(VK_F7) & 0x8000) {
        //    std::this_thread::sleep_for(std::chrono::milliseconds(200));
        //    debugMode = true;
        //    HideUID();
        //    debugMode = false;
        //}

        //if (GetAsyncKeyState(VK_F6) & 0x8000) {
        //    std::this_thread::sleep_for(std::chrono::milliseconds(200));

        //    if (uidHidden) {
        //        ShowUID();
        //    } else {
        //        HideUID();
        //    }
        //}


        bool shouldHide = (g_EnableHideUID == 1);

        if (g_EnableAdvan != lastEnableAdvan) {
            if (g_EnableAdvan == 0 && uidHidden) {
                ShowUID();
            }
            else if (g_EnableAdvan == 1 && shouldHide && !uidHidden) {
                HideUID();
            }
            lastEnableAdvan = g_EnableAdvan;
        }

        if (g_clearcache == 0 || g_clearcache == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (shouldHide != lastShouldHide) {
            if (!shouldHide && uidHidden) {
                ShowUID();
            }
            else if (shouldHide && !uidHidden) {
                HideUID();
            }
            lastShouldHide = shouldHide ? 1 : 0;
        }

        if (g_clearcache == 1) {                     // 用户点击了 "Clear Cache"
            ClearUIDCache();                         // 清除 UID 缓存
            g_clearcache = 0;                        // 重置状态，避免重复执行
            uidHidden = false;                       // 重置隐藏状态，强制重新搜索

            if (shouldHide) {                        // 如果当前需要隐藏UID
                HideUID();                           // 立即触发重新搜索并隐藏
            }

            OutputDebugStringA("[Info] Cache cleared and re-search triggered!\n");
        }


        //if (g_clearcache == 1) {
        //    std::this_thread::sleep_for(std::chrono::milliseconds(200));
        //    ClearUIDCache();
        //}

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    FreeLibraryAndExitThread(hModule, 0);
    return 0;
}

DWORD WINAPI MainThread(HMODULE hModule) {
    bool fovApplied = false;
    bool hasRecordedOriginalFov = false;
    float originalFov = 90.0f;
    int failCount = 0;
    const int maxFailCount = 100;

    while (!FindWindowA("UnrealWindow", nullptr)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    ScanAndInitOffsets();

    while (true) {
        //if (GetAsyncKeyState(VK_END) & 0x8000) {
        //    break;
        //}

        //char buf[64];
        //sprintf_s(buf, "[DLL] g_TargetFov = %d\n", g_TargetFov);
        //OutputDebugStringA(buf);

        APlayerController* pc = GetPlayerController();

        if (pc) {
            __try {
                APlayerCameraManager* camMgr = pc->PlayerCameraManager;

                if (!hasRecordedOriginalFov && camMgr && IsPointerReadable(camMgr)) {
                    __try {
                        originalFov = camMgr->DefaultFOV;
                        hasRecordedOriginalFov = true;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) {
                        originalFov = 90.0f;
                        hasRecordedOriginalFov = true;
                    }
                }

                if (g_EnableAdvan == 0) {
                    pc->FOV(0.0f);
                    fovApplied = false;
                }
                else if (g_EnableFov == 1) {
                    float targetFov = static_cast<float>(g_TargetFov);
                    pc->FOV(targetFov);
                    if (!fovApplied) {
                        fovApplied = true;
                    }
                }
                else {
                    pc->FOV(0.0f);
                    fovApplied = false;
                }

                failCount = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                failCount++;
            }
        }
        else {
            failCount++;
            if (failCount > maxFailCount) {
                failCount = 0;
                fovApplied = false;
                hasRecordedOriginalFov = false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    FreeLibraryAndExitThread(hModule, 0);
    return 0;
}





BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);

        // 检查是否是目标进程 260202
        HMODULE hWuWa = GetModuleHandleA("Client-Win64-Shipping.exe");
        HMODULE hGenshinImpact = GetModuleHandleA("GenshinImpactNAN.exe");
        HMODULE hStarRail = GetModuleHandleA("StarRialNAN.exe");

        // 如果不是目标进程，直接返回TRUE（DLL加载成功但不初始化）
        if (!hWuWa && !hGenshinImpact && !hStarRail) {
            return TRUE;
        }

        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread), hModule, 0, nullptr);

        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThreadUID), hModule, 0, nullptr);

        // 启动网络服务线程
        const auto hThreadNet = CreateThread(nullptr, 0, RunNetService, nullptr, 0, nullptr);
        if (!hThreadNet)
        {
            return OnWinError("CreateThreadNet", GetLastError());
        }
        CloseHandle(hThreadNet);

        break;
    }
    case DLL_PROCESS_DETACH: {
        break;
    }
    }
    return TRUE;
}
