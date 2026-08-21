#pragma once

/*
 * UEOffsets.hpp
 *
 * Header-only UE offset scanner — direct port of Dumper-7's scanner logic.
 */

#include <cstdint>
#include <optional>
#include <vector>
#include <functional>
#include <algorithm>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace UEOffsets
{

struct Offsets
{
    uint32_t GObjects        = 0;
    uint32_t AppendString    = 0;
    uint32_t GNames          = 0;
    uint32_t GWorld          = 0;
    uint32_t ProcessEvent    = 0;
    int32_t  ProcessEventIdx = 0;
};

namespace detail
{

// ============================================================
// Cached memory map
// ============================================================

struct MemRange { uintptr_t Base = 0; uintptr_t End = 0; };
static std::vector<MemRange> gValidRanges;

inline void BuildMemoryMap() noexcept
{
    gValidRanges.clear();
    uintptr_t Addr = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    while (VirtualQuery(reinterpret_cast<void*>(Addr), &mbi, sizeof(mbi)))
    {
        constexpr DWORD BAD = PAGE_NOACCESS | PAGE_GUARD;
        if ((mbi.State & MEM_COMMIT) && !(mbi.Protect & BAD))
        {
            uintptr_t RBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            gValidRanges.push_back({ RBase, RBase + mbi.RegionSize });
        }
        if (!mbi.RegionSize) break;
        uintptr_t Next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (Next <= Addr) break;
        Addr = Next;
    }
    std::sort(gValidRanges.begin(), gValidRanges.end(),
              [](const MemRange& A, const MemRange& B){ return A.Base < B.Base; });
}

inline bool IsBadReadPtr(const void* Ptr) noexcept
{
    if (!Ptr) return true;
    const uintptr_t Addr = reinterpret_cast<uintptr_t>(Ptr);
    size_t lo = 0, hi = gValidRanges.size();
    while (lo < hi)
    {
        const size_t mid = (lo + hi) / 2;
        if      (Addr >= gValidRanges[mid].End)  lo = mid + 1;
        else if (Addr <  gValidRanges[mid].Base) hi = mid;
        else return false;
    }
    return true;
}
inline bool IsBadReadPtr(uintptr_t Addr) noexcept { return IsBadReadPtr(reinterpret_cast<const void*>(Addr)); }

inline bool IsAddressInProcessRange(const void* Ptr) noexcept { return !IsBadReadPtr(Ptr); }
inline bool IsAddressInProcessRange(uintptr_t Addr) noexcept  { return !IsBadReadPtr(Addr); }

template<typename T>
inline bool SafeRead(uintptr_t Addr, T& Out) noexcept
{
    if (IsBadReadPtr(Addr)) return false;
    memcpy(&Out, reinterpret_cast<const void*>(Addr), sizeof(T));
    return true;
}

inline uintptr_t GetImageBase(const char* ModuleName = nullptr) noexcept
{
    return reinterpret_cast<uintptr_t>(
        ModuleName ? GetModuleHandleA(ModuleName) : GetModuleHandleA(nullptr));
}

inline uint32_t GetOffset(const void* AbsAddr, uintptr_t Base) noexcept
{
    const uintptr_t Abs = reinterpret_cast<uintptr_t>(AbsAddr);
    return (Abs > Base) ? static_cast<uint32_t>(Abs - Base) : 0u;
}

// ============================================================
// Cached PE sections
// ============================================================

struct SecInfo
{
    uintptr_t Base            = 0;
    uint32_t  Size            = 0;
    DWORD     Characteristics = 0;
};

static std::vector<SecInfo> gCachedSections;

inline std::vector<SecInfo> GetSectionsImpl(uintptr_t IB) noexcept
{
    std::vector<SecInfo> Out;
    if (!IB) return Out;
    auto* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(IB);
    auto* Nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(IB + Dos->e_lfanew);
    if (Nt->Signature != IMAGE_NT_SIGNATURE) return Out;
    const IMAGE_SECTION_HEADER* Sec = IMAGE_FIRST_SECTION(Nt);
    for (int i = 0; i < Nt->FileHeader.NumberOfSections; ++i, ++Sec)
    {
        if (!(Sec->Characteristics & IMAGE_SCN_MEM_READ)) continue;
        Out.push_back({ IB + Sec->VirtualAddress, Sec->Misc.VirtualSize, Sec->Characteristics });
    }
    return Out;
}

inline const std::vector<SecInfo>& GetSections() noexcept { return gCachedSections; }

inline void BuildCaches(uintptr_t IB) noexcept
{
    BuildMemoryMap();
    gCachedSections = GetSectionsImpl(IB);
}

// ============================================================
// Architecture helpers
// ============================================================

inline uintptr_t Resolve32BitRelativeCall(uintptr_t Addr) noexcept
{
    int32_t Rel = 0;
    memcpy(&Rel, reinterpret_cast<const void*>(Addr + 1), 4);
    return Addr + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(Rel));
}

inline uintptr_t Resolve32BitSectionRelativeCall(uintptr_t Addr) noexcept
{
    int32_t Rel = 0;
    memcpy(&Rel, reinterpret_cast<const void*>(Addr + 2), 4);
    return Addr + 6 + static_cast<uintptr_t>(static_cast<intptr_t>(Rel));
}

inline uintptr_t Resolve32BitRelativeMove(uintptr_t Addr) noexcept
{
    int32_t Rel = 0;
    memcpy(&Rel, reinterpret_cast<const void*>(Addr + 3), 4);
    return Addr + 7 + static_cast<uintptr_t>(static_cast<intptr_t>(Rel));
}

inline const uint8_t* ResolveJmpStub(const uint8_t* Ptr) noexcept
{
    if (Ptr && *Ptr == 0xE9)
    {
        int32_t Rel = 0;
        memcpy(&Rel, Ptr + 1, 4);
        uintptr_t Dest = reinterpret_cast<uintptr_t>(Ptr) + 5
                       + static_cast<uintptr_t>(static_cast<intptr_t>(Rel));
        if (!IsBadReadPtr(Dest))
            return reinterpret_cast<const uint8_t*>(Dest);
    }
    return Ptr;
}

inline uintptr_t FindNextFunctionStart(const void* Ptr) noexcept
{
    if (!Ptr) return 0;
    const uint8_t* P = static_cast<const uint8_t*>(Ptr);
    for (size_t i = 1; i < 0x400; ++i)
    {
        if (IsBadReadPtr(P + i)) break;
        if (P[i] == 0xCC || P[i] == 0x90)
        {
            size_t k = i;
            while (k < 0x400 && (P[k] == 0xCC || P[k] == 0x90)) ++k;
            if (k < 0x400) return reinterpret_cast<uintptr_t>(P + k);
        }
    }
    return 0;
}

// ============================================================
// Pattern scanner
// ============================================================

inline std::vector<int> ParseSig(const char* s) noexcept
{
    std::vector<int> out;
    for (const char* p = s; *p; )
    {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '?') { out.push_back(-1); ++p; if (*p == '?') ++p; }
        else { out.push_back(static_cast<int>(strtol(p, const_cast<char**>(&p), 16))); }
    }
    return out;
}

inline const uint8_t* FindPattern(const std::vector<int>& Sig,
                                   const uint8_t* Start, size_t Len) noexcept
{
    if (Sig.empty() || Len < Sig.size()) return nullptr;
    for (size_t i = 0; i <= Len - Sig.size(); ++i)
    {
        bool ok = true;
        for (size_t j = 0; j < Sig.size(); ++j)
            if (Sig[j] != -1 && Start[i + j] != static_cast<uint8_t>(Sig[j]))
            { ok = false; break; }
        if (ok) return Start + i;
    }
    return nullptr;
}

inline const uint8_t* FindPatternInModule(const char* SigStr,
                                           uintptr_t StartFrom = 0) noexcept
{
    auto Sig = ParseSig(SigStr);
    for (auto& Sec : GetSections())
    {
        if (!(Sec.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        if (StartFrom && (Sec.Base + Sec.Size) <= StartFrom) continue;
        size_t iStart = (StartFrom > Sec.Base) ? static_cast<size_t>(StartFrom - Sec.Base) : 0;
        const auto* Code = reinterpret_cast<const uint8_t*>(Sec.Base);
        size_t Avail = (Sec.Size > iStart) ? (Sec.Size - iStart) : 0;
        if (const auto* Hit = FindPattern(Sig, Code + iStart, Avail))
            return Hit;
    }
    return nullptr;
}

// ============================================================
// FIX 1: FindByStringInAllSections
//
// Pass 1 now collects ALL string occurrences across the entire module,
// regardless of StartAddr/Range. Only Pass 2 (LEA scan) is windowed.
// This matches Dumper-7's Platform::FindByStringInAllSections exactly.
// ============================================================

inline const void* FindByStringInAllSections(const wchar_t* Str, uintptr_t /*IB*/,
                                              uintptr_t StartAddr = 0,
                                              size_t    Range     = 0) noexcept
{
    const size_t StrBytes = (wcslen(Str) + 1) * sizeof(wchar_t);

    // Pass 1: collect ALL string occurrences (NO StartAddr/Range filtering here)
    std::vector<uintptr_t> StrLocs;
    for (auto& Sec : GetSections())
    {
        const auto* B = reinterpret_cast<const uint8_t*>(Sec.Base);
        if (Sec.Size < StrBytes) continue;
        for (size_t Off = 0; Off + StrBytes <= Sec.Size; ++Off)
            if (memcmp(B + Off, Str, StrBytes) == 0)
                StrLocs.push_back(Sec.Base + Off);
    }
    if (StrLocs.empty()) return nullptr;

    // Pass 2: find LEA in exec sections — windowed by StartAddr/Range
    for (auto& Sec : GetSections())
    {
        if (!(Sec.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto* Code = reinterpret_cast<const uint8_t*>(Sec.Base);

        size_t iStart = (StartAddr > Sec.Base) ? static_cast<size_t>(StartAddr - Sec.Base) : 0;
        size_t iEnd   = (Range && (iStart + Range) < Sec.Size) ? (iStart + Range) : Sec.Size;
        if (iEnd < 7) continue;

        for (size_t i = iStart; i + 7 <= iEnd; ++i)
        {
            if ((Code[i] != 0x48 && Code[i] != 0x4C) || Code[i + 1] != 0x8D) continue;
            int32_t Disp = 0;
            memcpy(&Disp, Code + i + 3, 4);
            uintptr_t Tgt = Sec.Base + i + 7 + static_cast<uintptr_t>(static_cast<intptr_t>(Disp));
            for (uintptr_t SA : StrLocs)
                if (Tgt == SA) return Code + i;
        }
    }
    return nullptr;
}

inline const void* FindNarrowStringRefInCode(const char* Str, uintptr_t /*IB*/,
                                              uintptr_t StartAddr = 0,
                                              size_t    Range     = 0) noexcept
{
    const size_t StrBytes = strlen(Str) + 1;

    // Pass 1: collect ALL string occurrences (NO StartAddr/Range filtering)
    std::vector<uintptr_t> StrLocs;
    for (auto& Sec : GetSections())
    {
        const auto* B = reinterpret_cast<const uint8_t*>(Sec.Base);
        if (Sec.Size < StrBytes) continue;
        for (size_t Off = 0; Off + StrBytes <= Sec.Size; ++Off)
            if (memcmp(B + Off, Str, StrBytes) == 0)
                StrLocs.push_back(Sec.Base + Off);
    }
    if (StrLocs.empty()) return nullptr;

    // Pass 2: find LEA — windowed by StartAddr/Range
    for (auto& Sec : GetSections())
    {
        if (!(Sec.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto* Code = reinterpret_cast<const uint8_t*>(Sec.Base);

        size_t iStart = (StartAddr > Sec.Base) ? static_cast<size_t>(StartAddr - Sec.Base) : 0;
        size_t iEnd   = (Range && (iStart + Range) < Sec.Size) ? (iStart + Range) : Sec.Size;
        if (iEnd < 7) continue;

        for (size_t i = iStart; i + 7 <= iEnd; ++i)
        {
            if ((Code[i] != 0x48 && Code[i] != 0x4C) || Code[i + 1] != 0x8D) continue;
            int32_t Disp = 0;
            memcpy(&Disp, Code + i + 3, 4);
            uintptr_t Tgt = Sec.Base + i + 7 + static_cast<uintptr_t>(static_cast<intptr_t>(Disp));
            for (uintptr_t SA : StrLocs)
                if (Tgt == SA) return Code + i;
        }
    }
    return nullptr;
}

// ============================================================
// Platform helpers
// ============================================================

inline uintptr_t GetAddressOfImportedFunctionFromAnyModule(const char* DllName,
                                                            const char* FuncName) noexcept
{
    HMODULE Dll = GetModuleHandleA(DllName);
    if (!Dll) return 0;
    return reinterpret_cast<uintptr_t>(GetProcAddress(Dll, FuncName));
}

using VTablePred = std::function<bool(const uint8_t*, int32_t)>;

inline std::pair<const void*, int32_t>
IterateVTableFunctions(void** Vft, const VTablePred& Pred, int MaxSlots = 0x150) noexcept
{
    for (int i = 0; i < MaxSlots; ++i)
    {
        uintptr_t Slot = reinterpret_cast<uintptr_t>(Vft[i]);
        if (!Slot || IsBadReadPtr(Slot)) break;
        const uint8_t* Fn = ResolveJmpStub(reinterpret_cast<const uint8_t*>(Slot));
        if (Pred(Fn, i))
            return { reinterpret_cast<const void*>(Fn), i };
    }
    return { nullptr, -1 };
}

inline std::vector<void*> FindAllAlignedValuesInProcess(const void* Value) noexcept
{
    std::vector<void*> Results;
    const uintptr_t Target = reinterpret_cast<uintptr_t>(Value);
    for (auto& Sec : GetSections())
    {
        if (Sec.Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        const auto* Base   = reinterpret_cast<const uint8_t*>(Sec.Base);
        const uint32_t Lim = (Sec.Size >= sizeof(uintptr_t))
                             ? Sec.Size - static_cast<uint32_t>(sizeof(uintptr_t)) : 0;
        for (uint32_t Off = 0; Off < Lim; Off += sizeof(uintptr_t))
        {
            uintptr_t Val = 0;
            memcpy(&Val, Base + Off, sizeof(Val));
            if (Val == Target)
                Results.push_back(const_cast<uint8_t*>(Base + Off));
        }
    }
    return Results;
}

// ============================================================
// GObjects
// ============================================================

struct FFixedLayout   { int32_t ObjectsOffset, MaxObjectsOffset, NumObjectsOffset; };
struct FChunkedLayout { int32_t ObjectsOffset, MaxElementsOffset, NumElementsOffset,
                                MaxChunksOffset, NumChunksOffset; };

static constexpr FFixedLayout kFixedLayouts[] =
{
    { 0x00, 0x08, 0x0C },
};

static constexpr FChunkedLayout kChunkedLayouts[] =
{
    { 0x00, 0x10, 0x14, 0x18, 0x1C },  // Default UE4.21+
    { 0x10, 0x00, 0x04, 0x08, 0x0C },  // Back4Blood
    { 0x18, 0x10, 0x00, 0x14, 0x20 },  // Multiversus
    { 0x18, 0x00, 0x14, 0x10, 0x04 },  // MindsEye
};

// Tracks which layout was matched (needed for NumElements offset)
static int32_t gMatchedNumElementsOffset = 0x14; // default layout
static int32_t gMatchedObjectsOffset     = 0x00;

inline bool IsAddressValidGObjects(uintptr_t Addr, const FFixedLayout& L) noexcept
{
    constexpr uint32_t kItemSize = sizeof(void*) + sizeof(int32_t) + sizeof(int32_t);
    uintptr_t ObjsPtr = 0;
    if (!SafeRead(Addr + L.ObjectsOffset, ObjsPtr))           return false;
    int32_t MaxEl = 0, NumEl = 0;
    if (!SafeRead<int32_t>(Addr + L.MaxObjectsOffset, MaxEl)) return false;
    if (!SafeRead<int32_t>(Addr + L.NumObjectsOffset, NumEl)) return false;
    if (NumEl > MaxEl || MaxEl > 0x400000 || NumEl < 0x1000)  return false;
    if (IsBadReadPtr(ObjsPtr))                                 return false;
    uintptr_t FifthObj = 0;
    if (!SafeRead(ObjsPtr + 5 * kItemSize, FifthObj))         return false;
    if (IsBadReadPtr(FifthObj))                                return false;
    int32_t InternalIdx = 0;
    if (!SafeRead<int32_t>(FifthObj + sizeof(void*) + sizeof(int32_t), InternalIdx)) return false;
    return InternalIdx == 5;
}

inline bool IsAddressValidGObjects(uintptr_t Addr, const FChunkedLayout& L) noexcept
{
    uintptr_t ObjsPtr = 0;
    if (!SafeRead(Addr + L.ObjectsOffset, ObjsPtr))            return false;
    int32_t MaxEl = 0, NumEl = 0, MaxCh = 0, NumCh = 0;
    if (!SafeRead<int32_t>(Addr + L.MaxElementsOffset, MaxEl)) return false;
    if (!SafeRead<int32_t>(Addr + L.NumElementsOffset, NumEl)) return false;
    if (!SafeRead<int32_t>(Addr + L.MaxChunksOffset,   MaxCh)) return false;
    if (!SafeRead<int32_t>(Addr + L.NumChunksOffset,   NumCh)) return false;
    if (NumCh > 0x14  || NumCh < 0x1)      return false;
    if (MaxCh > 0x5FF || MaxCh < 0x6)      return false;
    if (NumEl <= 0x800 || MaxEl <= 0x10000) return false;
    if (NumEl > MaxEl || NumCh > MaxCh)     return false;
    if ((MaxEl % 0x10) != 0)               return false;
    const int32_t EPC = MaxEl / MaxCh;
    if ((EPC % 0x10) != 0)                 return false;
    if (EPC < 0x8000 || EPC > 0x80000)     return false;
    if (((NumEl / EPC) + 1) != NumCh)      return false;
    if ((MaxEl / EPC) != MaxCh)            return false;
    if (IsBadReadPtr(ObjsPtr))             return false;
    for (int i = 0; i < NumCh; ++i)
    {
        uintptr_t ChunkPtr = 0;
        if (!SafeRead(ObjsPtr + i * sizeof(uintptr_t), ChunkPtr)) return false;
        if (!ChunkPtr || IsBadReadPtr(ChunkPtr))                   return false;
    }
    return true;
}

enum class ArrayKind { Fixed, Chunked };

inline std::optional<ArrayKind> MatchesAnyGObjectsLayout(uintptr_t Addr) noexcept
{
    for (auto& L : kFixedLayouts)
        if (IsAddressValidGObjects(Addr, L))
        {
            gMatchedNumElementsOffset = L.NumObjectsOffset;
            gMatchedObjectsOffset     = L.ObjectsOffset;
            return ArrayKind::Fixed;
        }
    for (auto& L : kChunkedLayouts)
        if (IsAddressValidGObjects(Addr, L))
        {
            gMatchedNumElementsOffset = L.NumElementsOffset;
            gMatchedObjectsOffset     = L.ObjectsOffset;
            return ArrayKind::Chunked;
        }
    return std::nullopt;
}

inline std::pair<uint32_t, ArrayKind> FindGObjects(uintptr_t IB) noexcept
{
    ArrayKind FoundKind = ArrayKind::Chunked;
    constexpr uint32_t kGran  = 0x4;
    constexpr uint32_t kGuard = 0x50;

    auto TrySection = [&](uintptr_t Base, uint32_t Size) -> uintptr_t
    {
        if (Size <= kGuard) return 0;
        const uint32_t Limit = Size - kGuard;
        for (uint32_t Off = 0; Off < Limit; Off += kGran)
        {
            if (auto K = MatchesAnyGObjectsLayout(Base + Off))
            { FoundKind = *K; return Base + Off; }
        }
        return 0;
    };

    auto* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(IB);
    auto* Nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(IB + Dos->e_lfanew);
    const IMAGE_SECTION_HEADER* Sec = IMAGE_FIRST_SECTION(Nt);
    for (int i = 0; i < Nt->FileHeader.NumberOfSections; ++i, ++Sec)
    {
        if (strncmp(reinterpret_cast<const char*>(Sec->Name), ".data", 5) != 0) continue;
        if (!(Sec->Characteristics & IMAGE_SCN_MEM_READ)) continue;
        if (uintptr_t Hit = TrySection(IB + Sec->VirtualAddress, Sec->Misc.VirtualSize))
            return { static_cast<uint32_t>(Hit - IB), FoundKind };
    }

    for (auto& S : GetSections())
    {
        if (S.Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        if (uintptr_t Hit = TrySection(S.Base, S.Size))
            return { static_cast<uint32_t>(Hit - IB), FoundKind };
    }
    return { 0u, ArrayKind::Chunked };
}

// FIX 3 (part): probe both FUObjectItem sizes (16 and 24 bytes)
// Returns the first valid object pointer found using the given item size.
inline uintptr_t GetObjectByIndexRaw(uintptr_t GObjectsAbs, int32_t Index, uint32_t ItemSize) noexcept
{
    constexpr uint32_t kPerChunk = 0x10000;
    uintptr_t ChunkArray = 0;
    if (!SafeRead(GObjectsAbs + gMatchedObjectsOffset, ChunkArray) || IsBadReadPtr(ChunkArray)) return 0;
    const int32_t ChunkIdx   = Index / kPerChunk;
    const int32_t InChunkIdx = Index % kPerChunk;
    uintptr_t ChunkPtr = 0;
    if (!SafeRead(ChunkArray + ChunkIdx * sizeof(uintptr_t), ChunkPtr)) return 0;
    if (!ChunkPtr || IsBadReadPtr(ChunkPtr)) return 0;
    uintptr_t ObjPtr = 0;
    SafeRead(ChunkPtr + InChunkIdx * ItemSize, ObjPtr);
    return ObjPtr;
}

// FIX 3: auto-detect FUObjectItem size (16 or 24) by checking object[0]
static uint32_t gDetectedItemSize = 16;

inline void DetectItemSize(uintptr_t GObjectsAbs) noexcept
{
    for (uint32_t sz : { 16u, 24u })
    {
        uintptr_t Obj = GetObjectByIndexRaw(GObjectsAbs, 0, sz);
        if (Obj && !IsBadReadPtr(Obj))
        {
            // Sanity: VFT must be readable two levels deep
            uintptr_t Vft = 0;
            if (SafeRead(Obj, Vft) && !IsBadReadPtr(Vft))
            {
                uintptr_t Slot0 = 0;
                if (SafeRead(Vft, Slot0) && !IsBadReadPtr(Slot0))
                {
                    gDetectedItemSize = sz;
                    return;
                }
            }
        }
    }
}

inline uintptr_t GetObjectByIndex(uintptr_t GObjectsAbs, int32_t Index) noexcept
{
    return GetObjectByIndexRaw(GObjectsAbs, Index, gDetectedItemSize);
}

inline uintptr_t GetFirstValidObject(uintptr_t GObjectsAbs) noexcept
{
    int32_t NumElements = 0;
    if (!SafeRead<int32_t>(GObjectsAbs + gMatchedNumElementsOffset, NumElements)) return 0;
    const int32_t Limit = (NumElements < 0x400) ? NumElements : 0x400;
    for (int32_t i = 0; i < Limit; ++i)
    {
        uintptr_t Obj = GetObjectByIndex(GObjectsAbs, i);
        if (Obj && !IsBadReadPtr(Obj))
            return Obj;
    }
    return 0;
}

// ============================================================
// AppendString
// ============================================================
struct NameFuncs { uint32_t AppendString = 0; uint32_t GetNameEntry = 0; };

inline NameFuncs FindNameFunctions(uintptr_t IB) noexcept
{
    const void* StringRef = FindNarrowStringRefInCode("ForwardShadingQuality_", IB);
    if (StringRef)
    {
        static constexpr const char* PossibleSigs[] =
        {
            "48 8D ?? ?? 48 8D ?? ?? E8",
            "48 8D ?? ?? ?? 48 8D ?? ?? E8",
            "48 8D ?? ?? 49 8B ?? E8",
            "48 8D ?? ?? ?? 49 8B ?? E8",
            "48 8D ?? ?? 48 8B ?? E8",
            "48 8D ?? ?? ?? 48 8B ?? E8",
        };
        const uint8_t* AppendStringAddr = nullptr;
        for (auto* SigStr : PossibleSigs)
        {
            auto Sig = ParseSig(SigStr);
            AppendStringAddr = FindPattern(Sig, static_cast<const uint8_t*>(StringRef), 0x50);
            if (AppendStringAddr) break;
        }
        if (AppendStringAddr)
        {
            for (int i = 0; i < 16; ++i)
            {
                if (AppendStringAddr[i] == 0xE8)
                {
                    uintptr_t Target = Resolve32BitRelativeCall(
                        reinterpret_cast<uintptr_t>(AppendStringAddr + i));
                    if (!IsBadReadPtr(Target))
                        return { GetOffset(reinterpret_cast<const void*>(Target), IB), 0u };
                }
            }
        }
        {
            const uintptr_t SearchStart = (reinterpret_cast<uintptr_t>(StringRef) > 0x180)
                                          ? reinterpret_cast<uintptr_t>(StringRef) - 0x180 : 0;
            auto Sig = ParseSig("8B ?? ?? E8 ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ??");
            if (const void* InlineHit = FindPattern(Sig,
                    reinterpret_cast<const uint8_t*>(SearchStart),
                    reinterpret_cast<uintptr_t>(StringRef) - SearchStart + 0x180))
            {
                const uintptr_t Base = reinterpret_cast<uintptr_t>(InlineHit);
                uintptr_t GNE = Resolve32BitRelativeCall(Base + 3);
                uintptr_t AS  = Resolve32BitRelativeCall(Base + 16);
                if (!IsBadReadPtr(GNE) && !IsBadReadPtr(AS))
                    return { GetOffset(reinterpret_cast<const void*>(AS),  IB),
                             GetOffset(reinterpret_cast<const void*>(GNE), IB) };
            }
        }
    }

    const void* BoneRef = FindByStringInAllSections(L" Bone: ", IB);
    if (BoneRef)
    {
        static constexpr const char* BackupSigs[] =
        {
            "48 8B ?? 48 8B ?? ?? E8",
            "48 8B ?? ?? 48 89 ?? ?? E8",
            "48 8B ?? 48 89 ?? ?? ?? E8",
        };
        const uintptr_t SearchStart = (reinterpret_cast<uintptr_t>(BoneRef) > 0xB0)
                                      ? reinterpret_cast<uintptr_t>(BoneRef) - 0xB0 : 0;
        for (auto* SigStr : BackupSigs)
        {
            auto Sig = ParseSig(SigStr);
            if (const uint8_t* Hit = FindPattern(Sig,
                    reinterpret_cast<const uint8_t*>(SearchStart), 0x100))
            {
                for (int i = 0; i < 16; ++i)
                {
                    if (Hit[i] == 0xE8)
                    {
                        uintptr_t Target = Resolve32BitRelativeCall(
                            reinterpret_cast<uintptr_t>(Hit + i));
                        if (!IsBadReadPtr(Target))
                            return { GetOffset(reinterpret_cast<const void*>(Target), IB), 0u };
                    }
                }
            }
        }
    }

    const void* NoneRef = FindByStringInAllSections(L"None", IB);
    if (!NoneRef) return {};
    const uint8_t* P = static_cast<const uint8_t*>(NoneRef);
    for (size_t i = 0; i < 0x200; ++i)
    {
        if (IsBadReadPtr(reinterpret_cast<uintptr_t>(P + i))) break;
        if (P[i] == 0xE8)
        {
            uintptr_t Target = Resolve32BitRelativeCall(reinterpret_cast<uintptr_t>(P + i));
            if (!IsBadReadPtr(Target))
                return { GetOffset(reinterpret_cast<const void*>(Target), IB), 0u };
        }
    }
    return {};
}

// ============================================================
// GNames — TryFindNameArray_Windows + TryFindNamePool_Windows
// ============================================================

inline std::pair<uintptr_t, bool>
FindFNameGetNamesOrGNames_Windows(uintptr_t ECSAddr, uintptr_t StartAddr, uintptr_t IB) noexcept
{
    constexpr int32_t ASMRelativeCallSizeBytes = 0x6;
    constexpr int32_t GetNamesCallSearchRange  = 0x150;

    // NOTE: StartAddr here is the address to start the CODE search from in Pass 2
    // Pass 1 in FindByStringInAllSections is now unconditionally global (FIX 1).
    const uint8_t* BytePropertyAddr = static_cast<const uint8_t*>(
        FindByStringInAllSections(L"ByteProperty", IB, StartAddr, 0));
    if (!BytePropertyAddr)
        return { 0x0, false };

    for (int i = 0; i < GetNamesCallSearchRange; i++)
    {
        if (BytePropertyAddr[-i] != 0xFF) continue;
        const uintptr_t CallTarget = Resolve32BitSectionRelativeCall(
            reinterpret_cast<uintptr_t>(BytePropertyAddr - i));
        if (CallTarget != ECSAddr) continue;

        const uintptr_t InstrAfterCall =
            reinterpret_cast<uintptr_t>(BytePropertyAddr) - (i - ASMRelativeCallSizeBytes);
        if (*reinterpret_cast<const uint8_t*>(InstrAfterCall) == 0xE8)
            return { Resolve32BitRelativeCall(InstrAfterCall), false };
        return { Resolve32BitRelativeMove(InstrAfterCall), true };
    }

    return FindFNameGetNamesOrGNames_Windows(
        ECSAddr,
        reinterpret_cast<uintptr_t>(BytePropertyAddr) + ASMRelativeCallSizeBytes,
        IB);
}

inline uint32_t TryFindNameArray_Windows(uintptr_t IB) noexcept
{
    using GetNameType = void* (*)();
    constexpr int32_t GetNamesCallSearchRange = 0x100;

    const uintptr_t ECSAddr =
        GetAddressOfImportedFunctionFromAnyModule("kernel32.dll", "EnterCriticalSection");
    if (!ECSAddr) return 0u;

    auto [Address, bIsGNamesDirectly] = FindFNameGetNamesOrGNames_Windows(ECSAddr, IB, IB);
    if (!Address) return 0u;

    if (bIsGNamesDirectly)
    {
        if (!IsAddressInProcessRange(Address) ||
            IsBadReadPtr(*reinterpret_cast<void**>(Address)))
            return 0u;
        return static_cast<uint32_t>(Address - IB);
    }

    void* Names = reinterpret_cast<GetNameType>(Address)();

    for (int i = 0; i < GetNamesCallSearchRange; i++)
    {
        if (*reinterpret_cast<const uint16_t*>(Address + i) != 0x8B48) continue;

        const uintptr_t MoveTarget = Resolve32BitRelativeMove(Address + i);
        if (!IsAddressInProcessRange(MoveTarget)) continue;

        const void* ValAtMove = *reinterpret_cast<void**>(MoveTarget);
        if (IsBadReadPtr(ValAtMove) || ValAtMove != Names) continue;

        return static_cast<uint32_t>(MoveTarget - IB);
    }
    return 0u;
}

inline uint32_t TryFindNamePool_Windows(uintptr_t IB) noexcept
{
    constexpr int32_t InitSRWLockSearchRange  = 0x50;

    const uintptr_t InitSRWLockAddr =
        GetAddressOfImportedFunctionFromAnyModule("kernel32.dll", "InitializeSRWLock");
    const uintptr_t RtlInitSRWLockAddr =
        GetAddressOfImportedFunctionFromAnyModule("ntdll.dll", "RtlInitializeSRWLock");

    void*     NamePoolInstance = nullptr;
    uintptr_t SigOccurrence    = 0;

    while (!NamePoolInstance)
    {
        if (SigOccurrence > 0) SigOccurrence += 1;

        const uint8_t* SigHit = FindPatternInModule("48 8D 0D ?? ?? ?? ?? E8", SigOccurrence);
        if (!SigHit) break;
        SigOccurrence = reinterpret_cast<uintptr_t>(SigHit);

        constexpr int32_t SizeOfMovInstr = 0x7;
        const uintptr_t PossibleCtorAddr =
            Resolve32BitRelativeCall(SigOccurrence + SizeOfMovInstr);

        if (!IsAddressInProcessRange(PossibleCtorAddr)) continue;

        for (int i = 0; i < InitSRWLockSearchRange; i++)
        {
            if (*reinterpret_cast<const uint16_t*>(PossibleCtorAddr + i) != 0x15FF) continue;

            const uintptr_t CallTarget =
                Resolve32BitSectionRelativeCall(PossibleCtorAddr + i);

            if (!IsAddressInProcessRange(CallTarget)) continue;

            const uintptr_t ValueAtCallTarget = *reinterpret_cast<uintptr_t*>(CallTarget);

            if (ValueAtCallTarget != InitSRWLockAddr && ValueAtCallTarget != RtlInitSRWLockAddr)
                continue;

            // The LEA target at SigOccurrence is FNamePool
            const uintptr_t FNamePoolAddr =
                Resolve32BitRelativeMove(SigOccurrence); // 48 8D 0D is a LEA rcx,[rip+rel32]

            if (!IsAddressInProcessRange(FNamePoolAddr)) continue;

            NamePoolInstance = reinterpret_cast<void*>(FNamePoolAddr);
            return static_cast<uint32_t>(FNamePoolAddr - IB);
        }
    }
    return 0u;
}

inline uint32_t FindGNames(uintptr_t IB) noexcept
{
    // Try TNameEntryArray path first
    if (uint32_t R = TryFindNameArray_Windows(IB)) return R;
    // Fall back to FNamePool path
    return TryFindNamePool_Windows(IB);
}

// ============================================================
// FIX 2: FindGWorld
//
// Uses a class-chain name walk to positively identify UWorld objects
// instead of the fragile "OuterPrivate == null" heuristic.
// Also uses gMatchedNumElementsOffset so the count read is always correct.
// ============================================================

// Check if a UObject (or its class chain) has class name "World".
// Walks up to 8 super-class steps. Uses GNames RVA to resolve FName if available.
inline bool IsUWorldObject(uintptr_t ObjPtr, uintptr_t GNamesAbs) noexcept
{
    // UObject standard layout (UE4.21+ / UE5):
    //  +0x00 VFT
    //  +0x08 ObjFlags (int32)
    //  +0x0C InternalIndex (int32)
    //  +0x10 ClassPrivate (UClass*)
    //  +0x18 FName { CompIdx int32, Number int32 }
    //  +0x20 OuterPrivate (UObject*)

    uintptr_t ClassPtr = 0;
    if (!SafeRead(ObjPtr + 0x10, ClassPtr) || !ClassPtr || IsBadReadPtr(ClassPtr))
        return false;

    // Walk the class chain up to 8 levels
    for (int depth = 0; depth < 8; ++depth)
    {
        // Read FName of this class (at +0x18 from the class object itself)
        int32_t CompIdx = 0;
        if (!SafeRead<int32_t>(ClassPtr + 0x18, CompIdx)) return false;

        // If we have GNames, try to resolve to "World"
        if (GNamesAbs)
        {
            // TNameEntryArray: GNames -> ptr -> chunk[0] -> FNameEntry
            // FNamePool: GNames is the pool directly
            // Try both: read GNames as a pointer-to-pointer first (TNameEntryArray style)
            uintptr_t NamesPtr = 0;
            if (SafeRead(GNamesAbs, NamesPtr) && !IsBadReadPtr(NamesPtr))
            {
                // Chunked name array: chunk index = CompIdx / 0x4000
                const int32_t ChunkIdx   = CompIdx / 0x4000;
                const int32_t InChunkIdx = CompIdx % 0x4000;
                uintptr_t ChunkPtr = 0;
                if (SafeRead(NamesPtr + ChunkIdx * sizeof(uintptr_t), ChunkPtr) && !IsBadReadPtr(ChunkPtr))
                {
                    // FNameEntry string is at +0x10 (typical StringOffset for UE4)
                    // Try offsets 0x10, 0x0C, 0x08 for the string
                    for (int strOff : { 0x10, 0x0C, 0x08, 0x06, 0x02 })
                    {
                        uintptr_t EntryAddr = ChunkPtr + InChunkIdx * sizeof(void*);
                        uintptr_t Entry = 0;
                        if (!SafeRead(EntryAddr, Entry) || IsBadReadPtr(Entry)) continue;
                        char Name[32] = {};
                        if (IsBadReadPtr(Entry + strOff)) continue;
                        memcpy(Name, reinterpret_cast<const void*>(Entry + strOff), 5);
                        if (memcmp(Name, "World", 5) == 0) return true;
                    }
                }
            }
        }

        // Fallback: no GNames yet — check OuterPrivate == null (outer-less object)
        // AND the class chain has a readable SuperStruct pointer at a plausible offset.
        // This is the original heuristic but only as last resort.

        // Walk SuperStruct — try +0x30 (common UE4.21+ offset for UStruct::SuperStruct)
        uintptr_t SuperPtr = 0;
        if (!SafeRead(ClassPtr + 0x30, SuperPtr)) return false;
        if (!SuperPtr) break; // reached top of hierarchy without finding "World"
        if (IsBadReadPtr(SuperPtr)) return false;
        ClassPtr = SuperPtr;
    }

    return false;
}

inline uint32_t FindGWorld(uintptr_t IB, uintptr_t GObjectsAbs, uintptr_t GNamesAbs) noexcept
{
    if (!GObjectsAbs) return 0u;

    int32_t NumElements = 0;
    if (!SafeRead<int32_t>(GObjectsAbs + gMatchedNumElementsOffset, NumElements)) return 0u;
    if (NumElements <= 0 || NumElements > 4000000) return 0u;

    constexpr int32_t RF_CDO = 0x10;

    for (int32_t Idx = 0; Idx < NumElements; ++Idx)
    {
        uintptr_t ObjPtr = GetObjectByIndex(GObjectsAbs, Idx);
        if (!ObjPtr || IsBadReadPtr(ObjPtr)) continue;

        int32_t Flags = 0;
        if (!SafeRead<int32_t>(ObjPtr + 0x08, Flags)) continue;
        if (Flags & RF_CDO) continue;

        uintptr_t ClassPtr = 0;
        if (!SafeRead(ObjPtr + 0x10, ClassPtr) || !ClassPtr || IsBadReadPtr(ClassPtr)) continue;

        // Quick pre-filter: read the class's FName CompIdx — "World" FName CompIdx
        // is small and fixed for any given game session. Skip non-class-like objects fast.
        int32_t ClassNameIdx = 0;
        if (!SafeRead<int32_t>(ClassPtr + 0x18, ClassNameIdx)) continue;
        if (ClassNameIdx <= 0) continue;

        // Check using IsUWorldObject
        if (!IsUWorldObject(ObjPtr, GNamesAbs)) continue;

        auto Results = FindAllAlignedValuesInProcess(reinterpret_cast<const void*>(ObjPtr));
        if (Results.empty()) continue;

        void* Result = nullptr;
        if (Results.size() == 1)
        {
            Result = Results[0];
        }
        else if (Results.size() == 2)
        {
            auto ObjAddress      = ObjPtr;
            auto* PossibleGWorld = reinterpret_cast<volatile uintptr_t*>(Results[0]);
            auto  CurrentValue   = *PossibleGWorld;
            for (int i = 0; CurrentValue == ObjAddress && i < 50; ++i)
            {
                ::Sleep(1);
                CurrentValue = *PossibleGWorld;
            }
            Result = (CurrentValue == ObjAddress) ? Results[0] : Results[1];
        }
        else
        {
            Result = Results[0];
        }

        if (Result)
            return GetOffset(Result, IB);
    }

    // Fallback: original heuristic (OuterPrivate == null, non-CDO)
    // Used when IsUWorldObject can't resolve names (no GNames, no SuperStruct chain)
    for (int32_t Idx = 0; Idx < NumElements && Idx < 0x10000; ++Idx)
    {
        uintptr_t ObjPtr = GetObjectByIndex(GObjectsAbs, Idx);
        if (!ObjPtr || IsBadReadPtr(ObjPtr)) continue;

        int32_t Flags = 0;
        if (!SafeRead<int32_t>(ObjPtr + 0x08, Flags)) continue;
        if (Flags & RF_CDO) continue;

        uintptr_t ClassPtr = 0;
        if (!SafeRead(ObjPtr + 0x10, ClassPtr) || !ClassPtr || IsBadReadPtr(ClassPtr)) continue;

        uintptr_t OuterPtr = 0;
        if (!SafeRead(ObjPtr + 0x20, OuterPtr)) continue;
        if (OuterPtr != 0) continue;

        uintptr_t Meta = 0;
        if (!SafeRead(ClassPtr + 0x10, Meta) || !Meta || IsBadReadPtr(Meta)) continue;

        auto Results = FindAllAlignedValuesInProcess(reinterpret_cast<const void*>(ObjPtr));
        if (Results.empty()) continue;

        void* Result = nullptr;
        if (Results.size() == 1)
        {
            Result = Results[0];
        }
        else if (Results.size() == 2)
        {
            auto ObjAddress      = ObjPtr;
            auto* PossibleGWorld = reinterpret_cast<volatile uintptr_t*>(Results[0]);
            auto  CurrentValue   = *PossibleGWorld;
            for (int i = 0; CurrentValue == ObjAddress && i < 50; ++i)
            {
                ::Sleep(1);
                CurrentValue = *PossibleGWorld;
            }
            Result = (CurrentValue == ObjAddress) ? Results[0] : Results[1];
        }
        else
        {
            Result = Results[0];
        }

        if (Result)
            return GetOffset(Result, IB);
    }

    return 0u;
}

// ============================================================
// FIX 3: FindProcessEvent
//
// Uses the dynamically detected gDetectedItemSize (set by DetectItemSize).
// The FunctionFlags pattern byte is wildcarded (??), so it already works for
// any flags offset. The real fix is that GetObjectByIndex now uses the correct
// item size so VFT[0] is actually a valid UObject.
// ============================================================

inline std::pair<uint32_t, int32_t>
FindProcessEvent(uintptr_t IB, uintptr_t GObjectsAbs) noexcept
{
    uintptr_t Obj0 = GetFirstValidObject(GObjectsAbs);
    if (!Obj0) return {};

    uintptr_t VftAddr = 0;
    if (!SafeRead(Obj0, VftAddr) || IsBadReadPtr(VftAddr)) return {};
    void** Vft = reinterpret_cast<void**>(VftAddr);

    // Primary: pattern-match FunctionFlags TEST instructions
    // { 0xF7, -1, FuncFlagsOffset_byte, 0,0,0,0, 0x04,0,0 }  FUNC_Native
    // { 0xF7, -1, FuncFlagsOffset_byte, 0,0,0,0, 0x00,0x40,0 } FUNC_BlueprintEvent
    // The FuncFlagsOffset byte is wildcarded (sig[2] == -1 == ??) so any offset works.
    const std::vector<int> SigNative = ParseSig("F7 ?? ?? 00 00 00 00 04 00 00");
    const std::vector<int> SigEvent  = ParseSig("F7 ?? ?? 00 00 00 00 00 40 00");

    auto IsPE = [&](const uint8_t* Fn, int32_t) -> bool
    {
        if (IsBadReadPtr(Fn)) return false;
        return FindPattern(SigNative, Fn, 0x400) != nullptr &&
               FindPattern(SigEvent,  Fn, 0xF00) != nullptr;
    };

    auto [FnPtr, FnIdx] = IterateVTableFunctions(Vft, IsPE);

    if (!FnPtr)
    {
        // Fallback: L"Accessed None" → next function after padding
        const void* StringRefAddr = FindByStringInAllSections(L"Accessed None", IB);
        if (!StringRefAddr) return {};

        const uintptr_t PossiblePE = FindNextFunctionStart(StringRefAddr);
        if (!PossiblePE) return {};

        auto IsSameAddr = [PossiblePE](const uint8_t* Fn, int32_t) -> bool
        {
            return reinterpret_cast<uintptr_t>(Fn) == PossiblePE;
        };
        auto [FnPtr2, FnIdx2] = IterateVTableFunctions(Vft, IsSameAddr);
        FnPtr = FnPtr2;
        FnIdx = FnIdx2;
    }

    if (!FnPtr) return {};
    return { GetOffset(FnPtr, IB), FnIdx };
}

} // namespace detail

// ============================================================
// Public API
// ============================================================

[[nodiscard]]
inline std::optional<Offsets> Scan(const char* ModuleName = nullptr) noexcept
{
    const uintptr_t IB = detail::GetImageBase(ModuleName);
    if (!IB) return std::nullopt;

    // Build caches once
    detail::BuildCaches(IB);

    Offsets Out{};

    // 1. GObjects
    auto [GObjectsRva, ArrayKind] = detail::FindGObjects(IB);
    if (!GObjectsRva) return std::nullopt;
    Out.GObjects = GObjectsRva;

    const uintptr_t GObjectsAbs = IB + GObjectsRva;

    // Detect FUObjectItem size before anything else that reads objects
    detail::DetectItemSize(GObjectsAbs);

    // 2. AppendString
    auto NameFns = detail::FindNameFunctions(IB);
    Out.AppendString = NameFns.AppendString;

    // 3. GNames
    Out.GNames = detail::FindGNames(IB);
    const uintptr_t GNamesAbs = Out.GNames ? (IB + Out.GNames) : 0;

    // 4. GWorld — pass GNames so IsUWorldObject can resolve names
    Out.GWorld = detail::FindGWorld(IB, GObjectsAbs, GNamesAbs);

    // 5. ProcessEvent
    auto [PERva, PEIdx] = detail::FindProcessEvent(IB, GObjectsAbs);
    Out.ProcessEvent    = PERva;
    Out.ProcessEventIdx = PEIdx;

    return Out;
}

} // namespace UEOffsets