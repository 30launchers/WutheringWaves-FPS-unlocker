//#pragma once
//#include <windows.h>
//#include <vector>
//#include <string>
//#include <sstream>
//#include <Psapi.h>
//
//#pragma comment(lib, "psapi.lib")
//
//namespace PatternScanner {
//
//    struct RegionInfo {
//        uintptr_t base;
//        size_t size;
//    };
//
//    inline bool IsReadableOrExecutable(DWORD protect) {
//        return
//            protect == PAGE_EXECUTE_READ ||
//            protect == PAGE_EXECUTE_READWRITE ||
//            protect == PAGE_EXECUTE_WRITECOPY ||
//            protect == PAGE_EXECUTE ||
//            protect == PAGE_READONLY ||
//            protect == PAGE_READWRITE ||
//            protect == PAGE_WRITECOPY;
//    }
//
//    inline std::vector<RegionInfo> GetMemoryRegions() {
//        std::vector<RegionInfo> regions;
//        SYSTEM_INFO sysInfo = {};
//        GetSystemInfo(&sysInfo);
//
//        uintptr_t start = reinterpret_cast<uintptr_t>(sysInfo.lpMinimumApplicationAddress);
//        uintptr_t end = reinterpret_cast<uintptr_t>(sysInfo.lpMaximumApplicationAddress);
//
//        MEMORY_BASIC_INFORMATION mbi{};
//        while (start < end) {
//            if (VirtualQuery(reinterpret_cast<void*>(start), &mbi, sizeof(mbi)) == 0)
//                break;
//
//            if ((mbi.State == MEM_COMMIT) && IsReadableOrExecutable(mbi.Protect)) {
//                regions.push_back({ reinterpret_cast<uintptr_t>(mbi.BaseAddress), mbi.RegionSize });
//            }
//
//            start += mbi.RegionSize;
//        }
//        return regions;
//    }
//
//    inline void ParsePattern(const std::string& pattern, std::vector<std::pair<uint8_t, bool>>& parsed) {
//        std::istringstream iss(pattern);
//        std::string byteStr;
//
//        while (iss >> byteStr) {
//            if (byteStr == "?" || byteStr == "??") {
//                parsed.emplace_back(0x00, true);  // 通配符
//            }
//            else {
//                parsed.emplace_back(static_cast<uint8_t>(std::strtoul(byteStr.c_str(), nullptr, 16)), false);
//            }
//        }
//    }
//
//    // 不包含C++对象的安全匹配函数，避免使用 __try 与析构冲突
//    inline bool SafeCompare(uint8_t* base, size_t size, const std::vector<std::pair<uint8_t, bool>>* pattern, uintptr_t* result) {
//        __try {
//            size_t patternSize = pattern->size();
//            for (size_t i = 0; i <= size - patternSize; ++i) {
//                bool found = true;
//                for (size_t j = 0; j < patternSize; ++j) {
//                    if (!(*pattern)[j].second && base[i + j] != (*pattern)[j].first) {
//                        found = false;
//                        break;
//                    }
//                }
//                if (found) {
//                    *result = reinterpret_cast<uintptr_t>(&base[i]);
//                    return true;
//                }
//            }
//        }
//        __except (EXCEPTION_EXECUTE_HANDLER) {
//            // 忽略无效内存
//        }
//        return false;
//    }
//
//    // 新增：安全匹配多个结果的函数
//    inline void SafeCompareMultiple(uint8_t* base, size_t size, const std::vector<std::pair<uint8_t, bool>>* pattern, std::vector<uintptr_t>& results) {
//        __try {
//            size_t patternSize = pattern->size();
//            for (size_t i = 0; i <= size - patternSize; ++i) {
//                bool found = true;
//                for (size_t j = 0; j < patternSize; ++j) {
//                    if (!(*pattern)[j].second && base[i + j] != (*pattern)[j].first) {
//                        found = false;
//                        break;
//                    }
//                }
//                if (found) {
//                    results.push_back(reinterpret_cast<uintptr_t>(&base[i]));
//                }
//            }
//        }
//        __except (EXCEPTION_EXECUTE_HANDLER) {
//            // 忽略无效内存
//        }
//    }
//
//    inline uintptr_t Scan(const std::string& pattern) {
//        std::vector<std::pair<uint8_t, bool>> parsedPattern;
//        ParsePattern(pattern, parsedPattern);
//        auto regions = GetMemoryRegions();
//
//        for (const auto& region : regions) {
//            uintptr_t found = 0;
//            if (SafeCompare(reinterpret_cast<uint8_t*>(region.base), region.size, &parsedPattern, &found)) {
//                return found;
//            }
//        }
//
//        return 0;
//    }
//
//    // 新增：返回多个搜索结果的函数
//    inline std::vector<uintptr_t> MultipleScan(const std::string& pattern) {
//        std::vector<std::pair<uint8_t, bool>> parsedPattern;
//        ParsePattern(pattern, parsedPattern);
//        auto regions = GetMemoryRegions();
//        std::vector<uintptr_t> results;
//
//        for (const auto& region : regions) {
//            SafeCompareMultiple(reinterpret_cast<uint8_t*>(region.base), region.size, &parsedPattern, results);
//        }
//
//        return results;
//    }
//
//    // 解析跳转类指令，如 call/jmp（E8/E9）
//    // instructionAddr: 指令地址（E8/E9开头）
//    // offset: 跳转偏移位置（E8后面是 +1）
//    // instructionSize: 整个指令大小（E8 xx xx xx xx 是 5 字节）
//    inline uintptr_t ResolveRelativeAddress(uintptr_t instructionAddr, size_t offset = 1, size_t instructionSize = 5) {
//        int32_t rel = *reinterpret_cast<int32_t*>(instructionAddr + offset);
//        return instructionAddr + instructionSize + rel;
//    }
//
//    //251124 - 在指定内存区域内扫描模式
//    inline uintptr_t ScanInRegion(void* regionBase, size_t regionSize, const std::string& pattern) {
//        std::vector<std::pair<uint8_t, bool>> parsedPattern;
//        ParsePattern(pattern, parsedPattern);
//
//        uintptr_t found = 0;
//        if (SafeCompare(static_cast<uint8_t*>(regionBase), regionSize, &parsedPattern, &found)) {
//            return found;
//        }
//
//        return 0;
//    }
//
//    // 新增：在指定内存区域内扫描多个结果
//    inline std::vector<uintptr_t> MultipleScanInRegion(void* regionBase, size_t regionSize, const std::string& pattern) {
//        std::vector<std::pair<uint8_t, bool>> parsedPattern;
//        ParsePattern(pattern, parsedPattern);
//        std::vector<uintptr_t> results;
//
//        SafeCompareMultiple(static_cast<uint8_t*>(regionBase), regionSize, &parsedPattern, results);
//        return results;
//    }
//
//
//    //260130
//    // 新增：只扫描主模块（.exe自身），且支持跨段扫描
//    inline uintptr_t ScanMain(const std::string& pattern) {
//        std::vector<std::pair<uint8_t, bool>> parsedPattern;
//        ParsePattern(pattern, parsedPattern);
//
//        // 1. 获取主模块基址和大小
//        HMODULE hModule = GetModuleHandleA(NULL);
//        if (hModule == NULL) return 0;
//
//        MODULEINFO modInfo = {};
//        if (GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo)) == 0) return 0;
//
//        uintptr_t moduleBase = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
//        uintptr_t moduleEnd = moduleBase + modInfo.SizeOfImage;
//
//        // 2. 获取所有内存区域
//        auto allRegions = GetMemoryRegions();
//
//        // 3. 遍历区域，只处理在主模块范围内的区域
//        for (const auto& region : allRegions) {
//            // 如果当前区域的结束地址在模块开始之前，或者开始地址在模块结束之后，跳过
//            if ((region.base + region.size) < moduleBase || region.base >= moduleEnd)
//                continue;
//
//            // 计算交集（防止越界扫描到其他模块）
//            uintptr_t scanStart = (region.base < moduleBase) ? moduleBase : region.base;
//            uintptr_t scanEnd = (region.base + region.size > moduleEnd) ? moduleEnd : (region.base + region.size);
//            size_t scanSize = scanEnd - scanStart;
//
//            if (scanSize < parsedPattern.size()) continue;
//
//            uintptr_t found = 0;
//            if (SafeCompare(reinterpret_cast<uint8_t*>(scanStart), scanSize, &parsedPattern, &found)) {
//                return found;
//            }
//        }
//
//        return 0;
//    }
//
//}








#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <Psapi.h>
#include <chrono>
#include <atomic>
#include <cstdio>

#pragma comment(lib, "psapi.lib")

namespace PatternScanner {

#ifdef _DEBUG
#define PATTERN_SCAN_LOG_RESULT(...) PatternScanner::LogScanResult(__VA_ARGS__)
#define PATTERN_SCAN_LOG_BEGIN(idVar) \
    const uint32_t idVar = PatternScanner::NextScanIndex(); \
    const auto _ps_t0_##idVar = std::chrono::high_resolution_clock::now();
#define PATTERN_SCAN_LOG_END(idVar, pattern, moduleName, found, address) \
    do { \
        const auto _ps_t1_##idVar = std::chrono::high_resolution_clock::now(); \
        const double _ps_ms_##idVar = std::chrono::duration<double, std::milli>(_ps_t1_##idVar - _ps_t0_##idVar).count(); \
        PATTERN_SCAN_LOG_RESULT(idVar, pattern, moduleName, found, _ps_ms_##idVar, address); \
    } while (0)
#else
#define PATTERN_SCAN_LOG_RESULT(...) do { } while (0)
#define PATTERN_SCAN_LOG_BEGIN(idVar) do { } while (0)
#define PATTERN_SCAN_LOG_END(idVar, pattern, moduleName, found, address) do { } while (0)
#endif

    struct RegionInfo {
        uintptr_t base;
        size_t size;
    };

    inline std::atomic<uint32_t> g_scanIndex{ 0 };

    inline uint32_t NextScanIndex() {
        return g_scanIndex.fetch_add(1, std::memory_order_relaxed);
    }

    inline void LogScanResult(uint32_t id, const std::string& pattern, const wchar_t* moduleName, bool found, double ms, uintptr_t address) {
        if (moduleName && moduleName[0] != L'\0') {
            if (found) {
                std::printf("[PatternScanner] id=%u module=%ls pattern=\"%s\" time=%.3fms addr=%p\n",
                    id, moduleName, pattern.c_str(), ms, reinterpret_cast<void*>(address));
            }
            else {
                std::printf("[PatternScanner] id=%u module=%ls pattern=\"%s\" time=%.3fms addr=<not found>\n",
                    id, moduleName, pattern.c_str(), ms);
            }
        }
        else {
            if (found) {
                std::printf("[PatternScanner] id=%u module=<all> pattern=\"%s\" time=%.3fms addr=%p\n",
                    id, pattern.c_str(), ms, reinterpret_cast<void*>(address));
            }
            else {
                std::printf("[PatternScanner] id=%u module=<all> pattern=\"%s\" time=%.3fms addr=<not found>\n",
                    id, pattern.c_str(), ms);
            }
        }
    }

    // 多结果扫描专用日志（输出总数量）
    inline void LogMultipleScanResult(uint32_t id, const std::string& pattern, const wchar_t* moduleName, size_t count, double ms) {
        const wchar_t* modName = (moduleName && moduleName[0] != L'\0') ? moduleName : L"<all>";
        std::printf("[PatternScanner] id=%u module=%ls pattern=\"%s\" time=%.3fms found=%zu results\n",
            id, modName, pattern.c_str(), ms, count);
    }

    inline bool IsReadableOrExecutable(DWORD protect) {
        return
            protect == PAGE_EXECUTE_READ ||
            protect == PAGE_EXECUTE_READWRITE ||
            protect == PAGE_EXECUTE_WRITECOPY ||
            protect == PAGE_EXECUTE ||
            protect == PAGE_READONLY ||
            protect == PAGE_READWRITE ||
            protect == PAGE_WRITECOPY;
    }

    inline void ParsePattern(const std::string& pattern, std::vector<std::pair<uint8_t, bool>>& parsed) {
        std::istringstream iss(pattern);
        std::string byteStr;

        while (iss >> byteStr) {
            if (byteStr == "?" || byteStr == "??") {
                parsed.emplace_back(0x00, true);
            }
            else {
                parsed.emplace_back(static_cast<uint8_t>(std::strtoul(byteStr.c_str(), nullptr, 16)), false);
            }
        }
    }

    inline bool SafeCompare(uint8_t* base, size_t size,
        const std::vector<std::pair<uint8_t, bool>>* pattern,
        uintptr_t* result) {
        __try {
            const size_t patternSize = pattern->size();
            if (patternSize == 0 || size < patternSize) return false;

            for (size_t i = 0; i <= size - patternSize; ++i) {
                bool found = true;
                for (size_t j = 0; j < patternSize; ++j) {
                    if (!(*pattern)[j].second && base[i + j] != (*pattern)[j].first) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    *result = reinterpret_cast<uintptr_t>(&base[i]);
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }

    // === 新增：专门用于多结果搜索的内部匹配函数 ===
    // 找到匹配后会跳过整个 pattern 长度，防止 "? ? ? ?" 类通配符产生大量重叠匹配
    inline void FindAllMatches(uint8_t* base, size_t size,
        const std::vector<std::pair<uint8_t, bool>>& pattern,
        std::vector<uintptr_t>& outResults) {
        __try {
            const size_t patternSize = pattern.size();
            if (patternSize == 0 || size < patternSize) return;

            for (size_t i = 0; i <= size - patternSize;) {
                bool matched = true;
                for (size_t j = 0; j < patternSize; ++j) {
                    if (!pattern[j].second && base[i + j] != pattern[j].first) {
                        matched = false;
                        break;
                    }
                }
                if (matched) {
                    outResults.push_back(reinterpret_cast<uintptr_t>(&base[i]));
                    i += patternSize; // 跳过已匹配的部分，避免重叠结果
                }
                else {
                    ++i;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // 如果内存访问出错，直接放弃当前区块的后续扫描
        }
    }

    inline std::vector<RegionInfo> GetMemoryRegionsInRange(uintptr_t start, uintptr_t end) {
        std::vector<RegionInfo> regions;
        if (start >= end) return regions;

        MEMORY_BASIC_INFORMATION mbi{};
        uintptr_t cur = start;

        while (cur < end) {
            if (VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi)) == 0)
                break;

            const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t regionEnd = regionBase + mbi.RegionSize;

            const uintptr_t clippedBase = (regionBase < start) ? start : regionBase;
            const uintptr_t clippedEnd = (regionEnd > end) ? end : regionEnd;

            if (clippedBase < clippedEnd) {
                if (mbi.State == MEM_COMMIT && IsReadableOrExecutable(mbi.Protect)) {
                    regions.push_back({ clippedBase, static_cast<size_t>(clippedEnd - clippedBase) });
                }
            }

            cur = regionEnd;
            if (cur <= regionBase) break;
        }

        return regions;
    }

    inline bool GetModuleRange(const wchar_t* moduleName, uintptr_t& base, size_t& size) {
        HMODULE hMod = nullptr;

        if (moduleName == nullptr || moduleName[0] == L'\0') {
            hMod = GetModuleHandleW(nullptr);
        }
        else {
            hMod = GetModuleHandleW(moduleName);
        }

        if (!hMod) return false;

        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), hMod, &mi, sizeof(mi)))
            return false;

        base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        size = static_cast<size_t>(mi.SizeOfImage);
        return true;
    }

    inline uintptr_t Scan(const std::string& pattern) {
        PATTERN_SCAN_LOG_BEGIN(scanId);
        std::vector<std::pair<uint8_t, bool>> parsedPattern;
        ParsePattern(pattern, parsedPattern);

        SYSTEM_INFO sysInfo{};
        GetSystemInfo(&sysInfo);
        const uintptr_t start = reinterpret_cast<uintptr_t>(sysInfo.lpMinimumApplicationAddress);
        const uintptr_t end = reinterpret_cast<uintptr_t>(sysInfo.lpMaximumApplicationAddress);

        auto regions = GetMemoryRegionsInRange(start, end);
        for (const auto& region : regions) {
            uintptr_t found = 0;
            if (SafeCompare(reinterpret_cast<uint8_t*>(region.base), region.size, &parsedPattern, &found)) {
                PATTERN_SCAN_LOG_END(scanId, pattern, nullptr, true, found);
                return found;
            }
        }
        PATTERN_SCAN_LOG_END(scanId, pattern, nullptr, false, 0);
        return 0;
    }

    inline uintptr_t ScanModule(const std::string& pattern, const wchar_t* moduleName) {
        PATTERN_SCAN_LOG_BEGIN(scanId);
        uintptr_t modBase = 0;
        size_t modSize = 0;

        if (!GetModuleRange(moduleName, modBase, modSize)) {
            PATTERN_SCAN_LOG_END(scanId, pattern, moduleName, false, 0);
            return 0;
        }

        std::vector<std::pair<uint8_t, bool>> parsedPattern;
        ParsePattern(pattern, parsedPattern);

        const uintptr_t start = modBase;
        const uintptr_t end = modBase + modSize;

        auto regions = GetMemoryRegionsInRange(start, end);
        for (const auto& region : regions) {
            uintptr_t found = 0;
            if (SafeCompare(reinterpret_cast<uint8_t*>(region.base), region.size, &parsedPattern, &found)) {
                PATTERN_SCAN_LOG_END(scanId, pattern, moduleName, true, found);
                return found;
            }
        }
        PATTERN_SCAN_LOG_END(scanId, pattern, moduleName, false, 0);
        return 0;
    }

    // === 新增：在指定模块中搜索所有匹配的结果 ===
    inline std::vector<uintptr_t> MultipleScanModule(const std::string& pattern, const wchar_t* moduleName) {
        std::vector<uintptr_t> results;

#ifdef _DEBUG
        const uint32_t scanId = NextScanIndex();
        const auto t0 = std::chrono::high_resolution_clock::now();
#endif

        uintptr_t modBase = 0;
        size_t modSize = 0;

        if (!GetModuleRange(moduleName, modBase, modSize)) {
#ifdef _DEBUG
            const auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            LogMultipleScanResult(scanId, pattern, moduleName, 0, ms);
#endif
            return results;
        }

        std::vector<std::pair<uint8_t, bool>> parsedPattern;
        ParsePattern(pattern, parsedPattern);

        const uintptr_t start = modBase;
        const uintptr_t end = modBase + modSize;

        auto regions = GetMemoryRegionsInRange(start, end);
        for (const auto& region : regions) {
            FindAllMatches(reinterpret_cast<uint8_t*>(region.base), region.size, parsedPattern, results);
        }

#ifdef _DEBUG
        const auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        LogMultipleScanResult(scanId, pattern, moduleName, results.size(), ms);
#endif

        return results;
    }


    // 解析跳转类指令，如 call/jmp（E8/E9）
    // instructionAddr: 指令地址（E8/E9开头）
    // offset: 跳转偏移位置（E8后面是 +1）
    // instructionSize: 整个指令大小（E8 xx xx xx xx 是 5 字节）
    inline uintptr_t ResolveRelativeAddress(uintptr_t instructionAddr, size_t offset = 1, size_t instructionSize = 5) {
        int32_t rel = *reinterpret_cast<int32_t*>(instructionAddr + offset);
        return instructionAddr + instructionSize + rel;
    }

    // === 附赠常用重载：直接传入扫描到的地址，一键算出绝对地址 ===
    // 用法: uintptr_t targetAddr = PatternScanner::ResolveRelativeAddress(scanAddr);
    inline uintptr_t ResolveRelativeAddress(uintptr_t instructionAddr) {
        return ResolveRelativeAddress(instructionAddr, 1, 5);
    }

} // namespace PatternScanner
