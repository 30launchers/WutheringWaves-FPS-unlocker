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
#include "UEOffsets.hpp"

#include <SDK.hpp>
#include "src/MinHookManager.h"

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

// ==================== 新增：去除模糊相关全局变量 ====================
typedef void(*ProcessEventFn)(UObject*, UFunction*, void*);
static ProcessEventFn OriginalProcessEvent = nullptr;
static volatile bool g_hookInit = false;
static volatile ULONGLONG g_lastApplyTime = 0;
static ULONGLONG g_lastRestoreTime = 0;
static volatile bool g_firstApply = true;
static DWORD g_gameThreadId = 0;

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
static bool forceRefresh = false;

int totalDumpedCount = 0;
bool debugMode = false;

std::mutex uidMutex;
std::atomic<bool> searching(false);
std::atomic<bool> searchComplete(false);
std::atomic<int> activeThreads(0);
const int THREAD_COUNT = 100;



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

bool scanOffsetsDone = false;

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


    // 260821
    // ==================== Plan 1: Dumper-7 style structural scan ====================
    // Identifies GObjects / AppendString / GNames / GWorld / ProcessEvent by
    // structural characteristics instead of hardcoded byte patterns, so it
    // keeps working after game updates.
    bool dynamicScanOk = false;
    while (true) {
        auto result = UEOffsets::Scan(nullptr);

        if (result.has_value() && result->GObjects != 0 && result->AppendString != 0) {
            SDK::Offsets::SetGObjects(static_cast<int32>(result->GObjects));
            SDK::Offsets::SetAppendString(static_cast<int32>(result->AppendString));

            if (result->ProcessEvent != 0) {
                SDK::Offsets::SetProcessEvent(static_cast<int32>(result->ProcessEvent));
            }
            SDK::Offsets::SetProcessEventIdx(result->ProcessEventIdx);

            // Force-refresh the absolute addresses cached inside the SDK,
            // in case another thread lazily initialized them with a zero
            // offset before the scan completed.
            UObject::GObjects.InitManually(reinterpret_cast<void*>(baseAddress + result->GObjects));
            FName::InitManually(reinterpret_cast<void*>(baseAddress + result->AppendString));

            char dbgBuf[256];
            sprintf_s(dbgBuf, "[Scan] GObjects=0x%X AppendString=0x%X ProcessEvent=0x%X Idx=0x%X GWorld=0x%X\n",
                result->GObjects, result->AppendString, result->ProcessEvent, result->ProcessEventIdx, result->GWorld);
            //OutputDebugStringA(dbgBuf);

            dynamicScanOk = true;
            break;
        }

        // The game may not have loaded enough UObjects yet; retry later
        auto current_time = std::chrono::steady_clock::now();
        if (current_time - start_time >= timeout_duration) {
            break; // timed out, fall back to legacy pattern scan
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (dynamicScanOk) {
        scanOffsetsDone = true;
        return;
    }



    // ==================== 扫描 Pattern 1 ====================
    bool pattern1_found = false;
    while (true) {
        //auto results1 = PatternScanner::MultipleScanModule(pattern1, moduleName);
        auto results1 = PatternScanner::MultipleScanModule(_gobjects_code.c_str(), moduleName);

        if (!results1.empty() && results1[0] >= 0x10) {
            uintptr_t gObjectsAddr = results1[0] - 0x10;
            //printf("[+] Pattern1 Found: %016llX\n", (unsigned long long)gObjectsAddr);

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
            //printf("[+] Pattern2 Found: %016llX\n", (unsigned long long)appendStringAddr);

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

	// 标记扫描完成 260821
	scanOffsetsDone = true;
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
    //sprintf_s(threadBuf, "[Thread %d] Finished, range: %d-%d\n", threadId, startIdx, endIdx);
    //OutputDebugStringA(threadBuf);
}

void MultiThreadFindUID(ULevel* level) {
    AActor** actors = nullptr;
    int actorCount = 0;

    if (!SEH_GetActors(level, &actors, &actorCount) || !actors) return;

    char levelBuf[128];
    //sprintf_s(levelBuf, "[Level] Actors count: %d, Starting %d-thread search...\n", actorCount, THREAD_COUNT);
    //OutputDebugStringA(levelBuf);

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
    //OutputDebugStringA("[Search] All threads completed\n");
}

bool ValidateCachedWidgets() {
    if (!cacheValid || cachedCount <= 0) return false;

    int validCount = 0;
    for (int i = 0; i < cachedCount; i++) {
        UUIItem* widget = cachedUIDWidgets[i];
        if (!widget || !IsPointerReadable(widget)) {
            continue;
        }

        __try {
            widget->SetIsUIActive(false);
            validCount++;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }

    return validCount == cachedCount;
}

void ClearCache() {
    memset(cachedUIDWidgets, 0, sizeof(cachedUIDWidgets));
    cachedCount = 0;
    cacheValid = false;

    foundUIDWidgets = nullptr;
    foundCount = 0;
}

void HideUID() {

    if (cacheValid && cachedCount > 0 && !forceRefresh) {
        if (!ValidateCachedWidgets()) {
            ClearCache();
        }
        else {
            foundUIDWidgets = cachedUIDWidgets;
            foundCount = cachedCount;
        }
    }
    else {
        forceRefresh = false;

        foundUIDWidgets = widgetBuffer;
        foundCount = 0;
        totalDumpedCount = 0;

        UWorld* world = GetWorld();
        if (!world) {
            return;
        }

        ULevel** levels = nullptr;
        int levelCount = 0;
        ULevel* persistentLevel = nullptr;
        bool hasPersistent = false;

        if (!SEH_GetWorldLevels(world, &levels, &levelCount, &persistentLevel, &hasPersistent)) {
            return;
        }

        char buf[256];
        if (hasPersistent && persistentLevel) {
            MultiThreadFindUID(persistentLevel);
        }
        else if (levels && levelCount > 0) {
            for (int i = 0; i < levelCount; i++) {
                MultiThreadFindUID(levels[i]);
            }
        }
        else {
            if (levels) free(levels);
            return;
        }

        if (levels) free(levels);

        if (foundCount > 0) {
            memcpy(cachedUIDWidgets, widgetBuffer, sizeof(UUIItem*) * foundCount);
            cachedCount = foundCount;
            cacheValid = true;
        }
    }

    if (foundCount > 0) {
        uidHidden = true;
        //OutputDebugStringA("[Success] UID hidden!\n");

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
        //OutputDebugStringA("[Fail] No UID widgets matched!\n");
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
    //OutputDebugStringA("[Info] UID shown!\n");
}

void BackgroundSearchLoop() {
    searching.store(true);
    searchComplete.store(false);

    //OutputDebugStringA("[Background] Starting continuous UID search with 30 threads...\n");

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
                        //OutputDebugStringA("[Background] UID widgets cached (not hidden)\n");
                        break;
                    }
                }
            }
        }

        if (foundCount > 0 && g_EnableHideUID == 1) {
            searchComplete.store(true);
            //OutputDebugStringA("[Background] UID found and hidden!\n");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    searching.store(false);
}

void ClearUIDCache() {
    std::lock_guard<std::mutex> lock(uidMutex);

    memset(cachedUIDWidgets, 0, sizeof(cachedUIDWidgets));
    cachedCount = 0;
    cacheValid = false;
    forceRefresh = true;

    foundUIDWidgets = nullptr;
    foundCount = 0;

    searchComplete.store(false);
}

DWORD WINAPI MainThreadUID(HMODULE hModule) {
    while (!FindWindowA("UnrealWindow", nullptr)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    //OutputDebugStringA("\n========================================\n");
    //OutputDebugStringA("[wwuid] DLL loaded\n");
    //OutputDebugStringA("[wwuid] Auto-searching UID with 30 threads...\n");
    //OutputDebugStringA("Press F6 to hide/show UID\n");
    //OutputDebugStringA("Press F7 to dump UI widgets (debug)\n");
    //OutputDebugStringA("Press END to exit\n");
    //OutputDebugStringA("========================================\n\n");

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


        //bool shouldHide =
        //    (g_EnableAdvan == 1 && g_EnableHideUID == 1);

        //if (g_clearcache == 1)
        //{
        //    ClearUIDCache();
        //    g_clearcache = 0;
        //}

        //if (shouldHide)
        //{
        //    HideUID();
        //}
        //else
        //{
        //    ShowUID();
        //}

        //std::this_thread::sleep_for(std::chrono::milliseconds(150));




        bool shouldHide = (g_EnableHideUID == 1);

        // 高级功能关闭时，只恢复一次
        if (g_EnableAdvan == 0)
        {
            if (uidHidden)
            {
                ShowUID();
                uidHidden = false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            continue;
        }

        // 开启隐藏时，每150ms强制刷新
        if (shouldHide)
        {
            HideUID();
            uidHidden = true;
        }
        else
        {
            // 关闭隐藏时，只Show一次
            if (uidHidden)
            {
                ShowUID();
                uidHidden = false;
            }
        }

        // Clear Cache
        if (g_clearcache == 1)
        {
            ClearUIDCache();

            g_clearcache = 0;

            uidHidden = false;

            if (shouldHide)
            {
                HideUID();
                uidHidden = true;
            }

            //OutputDebugStringA("[Info] Cache cleared and re-search triggered!\n");
        }






        //bool shouldHide = (g_EnableHideUID == 1);

        //if (g_EnableAdvan != lastEnableAdvan) {
        //    if (g_EnableAdvan == 0 && uidHidden) {
        //        ShowUID();
        //    }
        //    else if (g_EnableAdvan == 1 && shouldHide && !uidHidden) {
        //        HideUID();
        //    }
        //    lastEnableAdvan = g_EnableAdvan;
        //}

        //if (g_clearcache == 0 || g_clearcache == 1) {
        //    std::this_thread::sleep_for(std::chrono::milliseconds(200));
        //}

        //if (shouldHide != lastShouldHide) {
        //    if (!shouldHide && uidHidden) {
        //        ShowUID();
        //    }
        //    else if (shouldHide && !uidHidden) {
        //        HideUID();
        //    }
        //    lastShouldHide = shouldHide ? 1 : 0;
        //}

        //if (g_clearcache == 1) {                     // 用户点击了 "Clear Cache"
        //    ClearUIDCache();                         // 清除 UID 缓存
        //    g_clearcache = 0;                        // 重置状态，避免重复执行
        //    uidHidden = false;                       // 重置隐藏状态，强制重新搜索

        //    if (shouldHide) {                        // 如果当前需要隐藏UID
        //        HideUID();                           // 立即触发重新搜索并隐藏
        //    }

        //    //OutputDebugStringA("[Info] Cache cleared and re-search triggered!\n");
        //}


        //if (g_clearcache == 1) {
        //    std::this_thread::sleep_for(std::chrono::milliseconds(200));
        //    ClearUIDCache();
        //}

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
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


static int removeblurmode = -1;
bool EnableRemoveBlur = 0;
int RemoveBlurMode = 0;
static bool g_EnableBlurFix = true;
// For Remove Dither: All Characters
static bool IsObjectValid(UObject* o)
{
    if (!o || reinterpret_cast<uintptr_t>(o) < 0x10000) {
        return false;
    }
    __try {
        void** v = *reinterpret_cast<void***>(o);
        if (!v || reinterpret_cast<uintptr_t>(v) < 0x10000) {
            return false;
        }
        int32 i = *reinterpret_cast<int32*>(reinterpret_cast<uintptr_t>(o) + 0x0C);
        return i >= 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { 
        return false; 
    }
}

static void FixAllCharacters_GetActors(UWorld* w, UClass* tsBaseCharClass, TArray<AActor*>* outActors)
{
    __try {
        UGameplayStatics::GetAllActorsOfClass(w, tsBaseCharClass, outActors);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

static bool FixSingleCharacter(UObject* ch)
{
    __try {
        if (!ch || !IsObjectValid(ch)) {
            return false;
        }
        auto* crc = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(ch) + 0x0698);
        if (!crc || !IsObjectValid(crc)) {
            return false;
        }

		// Check if we should disable blur based on the global setting
        bool disbaleBlur = true;
        if (g_EnableBlurFix) {
			disbaleBlur = true;
        }
        else {
            disbaleBlur = false;
        }

        UFunction* disFunc = crc->Class->GetFunction("CharRenderingComponent_C", "SetDisableFightDither");
        if (disFunc) {
            struct { bool disable; } parms;
            //parms.disable = true;
            parms.disable = disbaleBlur;
            crc->ProcessEvent(disFunc, &parms);
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static UClass* FindClassByNamePart(const char* part)
{
    for (int i = 0; i < UObject::GObjects->Num(); i++) {
        UObject* item = UObject::GObjects->GetByIndex(i);
        if (!item || !IsObjectValid(item)) {
            continue;
        }
        if (!item->IsA(UClass::StaticClass())) {
            continue;
        }
        UClass* cls = static_cast<UClass*>(item);
        if (cls && cls->GetName().find(part) != std::string::npos) {
            return cls;
        }
    }
    return nullptr;
}

static void FixAllCharacters(UWorld* w)
{
    if (!w || !IsObjectValid(w)){ 
        return; 
    }

    UClass* tsBaseCharClass = FindClassByNamePart("TsBaseCharacter_C");
    if (!tsBaseCharClass){
        return; 
    }

    TArray<AActor*> actors;
    FixAllCharacters_GetActors(w, tsBaseCharClass, &actors);

    int fixed = 0;
    for (int i = 0; i < actors.Num(); i++) {
        if (FixSingleCharacter(actors[i])) {
            fixed++;
        }
    }

    if (fixed > 0 && g_firstApply) {
        //LOG(L"[WWremoveblur] Fixed dither on %d characters", fixed);
    }
}

// For Remove Dither: PlayerOnly Character
static void FixPlayerCharacter(UWorld* w)
{
    if (!w || !IsObjectValid(w)){ 
        return; 
    }

    __try {
        APawn* playerPawn = UGameplayStatics::GetPlayerPawn(w, 0);
        if (!playerPawn || !IsObjectValid(playerPawn)) {
            return;
        }

        auto* crc = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(playerPawn) + 0x0698);
        if (!crc || !IsObjectValid(crc)) {
            return;
        }

        // Call SetDisableFightDither(true) via ProcessEvent
        UFunction* disFunc = crc->Class->GetFunction("CharRenderingComponent_C", "SetDisableFightDither");
        if (disFunc) {
            struct { bool disable; } parms;
            parms.disable = true;
            crc->ProcessEvent(disFunc, &parms);

            if (g_firstApply) {
                //LOG(L"[WWremoveblur] Fixed dither on player character");
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
		//LOG(L"[WWremoveblur] Exception occurred while fixing player character dither");
    }
}

static void ApplyDitherFix()
{
    UWorld* w = GetWorld();
    if (!w || !IsObjectValid(w)){ 
        return; 
    }
    FixPlayerCharacter(w);
    g_firstApply = false;
}

static void ApplyDitherFixMode2()
{
    UWorld* w = GetWorld();
    if (!w || !IsObjectValid(w)) {
        return;
    }
    FixAllCharacters(w);
    g_firstApply = false;
}

static void RestoreDither()
{
    UWorld* w = GetWorld();
    if (!w) { 
        return; 
    }

    APawn* playerPawn = UGameplayStatics::GetPlayerPawn(w, 0);
    if (!playerPawn) { 
        return; 
    }

    auto* crc = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(playerPawn) + 0x0698);
    if (!crc) { 
        return; 
    }

    UFunction* disFunc = crc->Class->GetFunction("CharRenderingComponent_C", "SetDisableFightDither");
    if (disFunc)
    {
        struct { bool disable; } parms;
        parms.disable = false;
        crc->ProcessEvent(disFunc, &parms);
    }
}


static void HookedProcessEvent(UObject* o, UFunction* f, void* p)
{
    OriginalProcessEvent(o, f, p);

    if (!g_hookInit) {
        return;
    }

    if (g_gameThreadId == 0) {
        g_gameThreadId = GetCurrentThreadId();
    }

    if (GetCurrentThreadId() != g_gameThreadId) {
        return;
    }

    ULONGLONG n = GetTickCount64();

    if (g_EnableBlurFix)
    {
        if (n - g_lastApplyTime >= 517)
        {
            g_lastApplyTime = n;

            if (removeblurmode == 0) {
                ApplyDitherFix();
            }

            if (removeblurmode == 1) {
                ApplyDitherFixMode2();
            }
        }
    }
    else
    {
        if (n - g_lastRestoreTime >= 1517)
        {
            g_lastRestoreTime = n;

            if(removeblurmode == 0) {
                RestoreDither();
			}

            if (removeblurmode == 1) {
                ApplyDitherFixMode2();
            }
        }
    }
}

DWORD WINAPI MainThreadBlur(HMODULE hModule) {

    while (true)
    {
        if (GetAsyncKeyState(VK_F8) & 1)
        {
            g_EnableBlurFix = !g_EnableBlurFix;
        }
        Sleep(50);
    }
}

// Resolve ProcessEvent RVA dynamically via vtable[ProcessEventIdx]
static int32 ResolveProcessEventRVA()
{
    __try {
        auto& objArray = *SDK::UObject::GObjects;
        if (!objArray.Num()) return 0;

        int count = objArray.Num();
        if (count > 50000) count = 50000;

        for (int i = 0; i < count; i++) {
            SDK::UObject* obj = objArray.GetByIndex(i);
            if (!obj) continue;

            void** vtable = *reinterpret_cast<void***>(obj);
            if (!vtable || reinterpret_cast<uintptr_t>(vtable) < 0x10000) continue;

            uintptr_t funcAddr = reinterpret_cast<uintptr_t>(vtable[Offsets::ProcessEventIdx]);
            if (funcAddr < 0x10000) continue;

            uintptr_t imageBase = (uintptr_t)GetModuleHandleA(NULL);
            return static_cast<int32>(funcAddr - imageBase);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}


DWORD WINAPI HookInitThread(LPVOID)
{
#ifdef ENABLE_CONSOLE
    InitConsole();
#endif

    //while (!FindWindowA("UnrealWindow", nullptr)) {
    //    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    //}

    while(!scanOffsetsDone) {
        Sleep(5);
	}
	Sleep(50);

    //Sleep(1015); 
    
    // Wait for GObjects to initialize, then dynamically calculate the ProcessEvent RVA through the vtable.
    //int maxRetry = 155;
    //while (maxRetry-- > 0)
    //{
    //    int32 rva = ResolveProcessEventRVA();
    //    if (rva != 0)
    //    {
    //        Offsets::SetProcessEvent(rva);
    //        break;
    //    }
    //    Sleep(500);
    //}

    // Wait for GObjects to initialize, then dynamically calculate the ProcessEvent RVA through the vtable.
    //auto startTime = std::chrono::steady_clock::now();
    //auto timeout = std::chrono::seconds(30);

    //while (std::chrono::steady_clock::now() - startTime < timeout)
    //{
    //    int32 rva = ResolveProcessEventRVA();
    //    if (rva != 0)
    //    {
    //        Offsets::SetProcessEvent(rva);
    //        break;
    //    }

    //    Sleep(5);
    //}


    //if (Offsets::ProcessEvent == 0)
    //{
    //    //LOG(L"[WWremoveblur] ResolveProcessEventRVA failed");
    //    MessageBoxA(NULL, "ResolveProcessEventRVA timed out!", "Error", MB_OK | MB_ICONERROR);
    //    return 1;
    //}

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    void* addr = (void*)(base + Offsets::ProcessEvent);  

    //if (!MinHookManager::Add(addr, &HookedProcessEvent, (void**)&OriginalProcessEvent)) {
    //    //LOG(L"[WWremoveblur] MinHookManager::Add failed");
    //    MessageBoxA(NULL, "ProcessEvent hook add failed!", "Error", MB_OK | MB_ICONERROR);
    //    return 1;
    //}


    auto hookStartTime = std::chrono::steady_clock::now();
    constexpr auto hookTimeout = std::chrono::seconds(30);

    while (true)
    {
        if (MinHookManager::Add(addr, &HookedProcessEvent,(void**)&OriginalProcessEvent)){
            // Hook 安装成功
            break;
        }

        auto currentTime = std::chrono::steady_clock::now();

        if (currentTime - hookStartTime >= hookTimeout)
        {
            MessageBoxA(NULL, "ProcessEvent hook add failed! Timeout after 30 seconds.", "Error", MB_OK | MB_ICONERROR);
            return 1;
        }

        Sleep(5);
    }


    g_hookInit = true;
    //LOG(L"[WWremoveblur] Hook active");
    return 0;
}

std::wstring g_ConfigPath;

static void InitConfig(HMODULE hModule)
{
    wchar_t dllPath[MAX_PATH]{};
    GetModuleFileNameW(hModule, dllPath, MAX_PATH);

    std::wstring path = dllPath;
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        path = path.substr(0, pos + 1);
    }

    g_ConfigPath = path + L"configww.ini";
    if (GetFileAttributesW(g_ConfigPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        WritePrivateProfileStringW(L"Configs", L"EnableRemoveBlur", L"0", g_ConfigPath.c_str());
        WritePrivateProfileStringW(L"Configs", L"RemoveBlurMode", L"0", g_ConfigPath.c_str());
    }
}


static void LoadConfig()
{
    EnableRemoveBlur = GetPrivateProfileIntW(L"Configs", L"EnableRemoveBlur", EnableRemoveBlur ? 1 : 0, g_ConfigPath.c_str()) != 0;
    RemoveBlurMode = GetPrivateProfileIntW(L"Configs",L"RemoveBlurMode", RemoveBlurMode, g_ConfigPath.c_str());
}

DWORD WINAPI LoadConfigThread(LPVOID lpParam)
{
    HMODULE hModule = (HMODULE)lpParam;

    InitConfig(hModule);
    LoadConfig();

    removeblurmode = RemoveBlurMode;
    if (EnableRemoveBlur == 1)
    {
		OutputDebugStringA("[Config] starting HookInitThread and MainThreadBlur...\n");
        CreateThread(nullptr, 0, HookInitThread, nullptr, 0, nullptr);
        //CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThreadBlur), hModule, 0, nullptr);
    }

    while (true)
    {
        LoadConfig();

        char buf[128];
        sprintf_s(buf, "[Config] EnableRemoveBlur=%d, RemoveBlurMode=%d\n", EnableRemoveBlur, RemoveBlurMode); 
        //OutputDebugStringA(buf);

		g_EnableBlurFix = (EnableRemoveBlur == 1);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

// ==================== DllMain ====================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);

        // 检查是否是目标进程
        HMODULE hWuWa = GetModuleHandleA("Client-Win64-Shipping.exe");
        HMODULE hGenshinImpact = GetModuleHandleA("GenshinImpactNAN.exe");
        HMODULE hStarRail = GetModuleHandleA("StarRailNAN.exe");

        if (!hWuWa && !hGenshinImpact && !hStarRail) {
            return TRUE;
        }

        CreateThread(nullptr, 0, LoadConfigThread, hModule, 0, nullptr);

        // 原有线程
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread), hModule, 0, nullptr);
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThreadUID), hModule, 0, nullptr);

        // 网络服务
        const auto hThreadNet = CreateThread(nullptr, 0, RunNetService, nullptr, 0, nullptr);
        if (!hThreadNet) {
            return OnWinError("CreateThreadNet", GetLastError());
        }
        CloseHandle(hThreadNet);

        // 启动Hook初始化线程
        //CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(CheckNeedEnableRemoveBlur), hModule, 0, nullptr);
        //CreateThread(nullptr, 0, HookInitThread, nullptr, 0, nullptr);
        //CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThreadBlur), hModule, 0, nullptr);

        break;
    }
    case DLL_PROCESS_DETACH: {
        // ========== 新增：清理 Hook ==========
        if (g_hookInit) {
            g_hookInit = false;
            uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
            void* addr = (void*)(base + Offsets::ProcessEvent);
            MinHookManager::Remove(addr);
        }
#ifdef ENABLE_CONSOLE
        if (g_hConOut != INVALID_HANDLE_VALUE) CloseHandle(g_hConOut);
        FreeConsole();
#endif
        break;
    }
    }
    return TRUE;
}