#include <windows.h>
#include <SDK.hpp>

using namespace SDK;

extern "C" {

    bool SEH_GetDisplayName(UUIItem* uiItem, wchar_t* outBuffer, int bufferSize) {
        __try {
            if (!uiItem) return false;

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(uiItem, &mbi, sizeof(mbi)) == 0) return false;
            constexpr DWORD readable_flags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;
            if ((mbi.State != MEM_COMMIT) || !(mbi.Protect & readable_flags) || (mbi.Protect & PAGE_GUARD)) return false;

            FString displayName = uiItem->GetDisplayName();
            const wchar_t* str = displayName.CStr();
            if (str && wcslen(str) > 0) {
                wcscpy_s(outBuffer, bufferSize, str);
                return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return false;
    }

    bool SEH_GetActors(ULevel* level, AActor*** outActors, int* outCount) {
        *outActors = nullptr;
        *outCount = 0;

        __try {
            if (!level) return false;

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(level, &mbi, sizeof(mbi)) == 0) return false;
            constexpr DWORD readable_flags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;
            if ((mbi.State != MEM_COMMIT) || !(mbi.Protect & readable_flags) || (mbi.Protect & PAGE_GUARD)) return false;

            TArray<AActor*> actorArray = level->Actors;
            int count = actorArray.Num();

            if (count <= 0) return true;

            AActor** actors = (AActor**)malloc(sizeof(AActor*) * count);
            if (!actors) return false;

            for (int i = 0; i < count; i++) {
                actors[i] = actorArray[i];
            }

            *outActors = actors;
            *outCount = count;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return false;
    }

    bool SEH_GetChildren(UUIItem* uiItem, UUIItem*** outChildren, int* outCount) {
        *outChildren = nullptr;
        *outCount = 0;

        __try {
            if (!uiItem) return false;

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(uiItem, &mbi, sizeof(mbi)) == 0) return false;
            constexpr DWORD readable_flags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;
            if ((mbi.State != MEM_COMMIT) || !(mbi.Protect & readable_flags) || (mbi.Protect & PAGE_GUARD)) return false;

            TArray<UUIItem*> childrenArray;
            uiItem->GetAllAttachUIChildren(&childrenArray);
            int count = childrenArray.Num();

            if (count <= 0) return true;

            UUIItem** children = (UUIItem**)malloc(sizeof(UUIItem*) * count);
            if (!children) return false;

            for (int i = 0; i < count; i++) {
                children[i] = childrenArray[i];
            }

            *outChildren = children;
            *outCount = count;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return false;
    }

    bool SEH_GetWorldLevels(UWorld* world, ULevel*** outLevels, int* outCount, ULevel** outPersistentLevel, bool* outHasPersistent) {
        *outLevels = nullptr;
        *outCount = 0;
        *outPersistentLevel = nullptr;
        *outHasPersistent = false;

        __try {
            if (!world) return false;

            ULevel* persistent = world->PersistentLevel;
            if (persistent) {
                MEMORY_BASIC_INFORMATION mbi;
                if (VirtualQuery(persistent, &mbi, sizeof(mbi)) != 0) {
                    constexpr DWORD readable_flags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                        PAGE_EXECUTE_WRITECOPY;
                    if ((mbi.State == MEM_COMMIT) && (mbi.Protect & readable_flags) && !(mbi.Protect & PAGE_GUARD)) {
                        *outPersistentLevel = persistent;
                        *outHasPersistent = true;
                        return true;
                    }
                }
            }

            TArray<ULevel*> levelsArray = world->Levels;
            int count = levelsArray.Num();

            if (count <= 0) return true;

            ULevel** levels = (ULevel**)malloc(sizeof(ULevel*) * count);
            if (!levels) return false;

            for (int i = 0; i < count; i++) {
                levels[i] = levelsArray[i];
            }

            *outLevels = levels;
            *outCount = count;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return false;
    }

    void SEH_ProcessActor(AActor* actor,
        bool debugMode,
        int* totalDumpedCount,
        UUIItem** foundUIDWidgets,
        int* foundCount,
        int MAX_FOUND,
        bool (*IsUIDWidgetName)(const wchar_t*)) {
        __try {
            if (!actor) return;

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(actor, &mbi, sizeof(mbi)) == 0) return;
            constexpr DWORD readable_flags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;
            if ((mbi.State != MEM_COMMIT) || !(mbi.Protect & readable_flags) || (mbi.Protect & PAGE_GUARD)) return;

            if (actor->StaticClass() == ALGUIManagerActor::StaticClass()) {
                //OutputDebugStringA("[>>> Found ALGUIManagerActor! <<<]\n");

                ALGUIManagerActor* manager = static_cast<ALGUIManagerActor*>(actor);
                if (!manager) return;

                if (VirtualQuery(manager, &mbi, sizeof(mbi)) == 0) return;
                if ((mbi.State != MEM_COMMIT) || !(mbi.Protect & readable_flags) || (mbi.Protect & PAGE_GUARD)) return;

                TArray<TWeakObjectPtr<UUIItem>> allUIItems = manager->AllUIItem;

                char countBuf[128];
                sprintf_s(countBuf, "AllUIItems count: %d\n", allUIItems.Num());
                //OutputDebugStringA(countBuf);

                for (int i = 0; i < allUIItems.Num(); i++) {
                    UUIItem* uiItem = allUIItems[i].Get();

                    wchar_t nameBuf[256] = { 0 };
                    if (SEH_GetDisplayName(uiItem, nameBuf, 256)) {
                        if (debugMode && (*totalDumpedCount < 2000)) {
                            char buf[512];
                            sprintf_s(buf, "[%d] %ls\n", (*totalDumpedCount)++, nameBuf);
                            //OutputDebugStringA(buf);
                        }

                        if (IsUIDWidgetName(nameBuf)) {
                            if (*foundCount >= MAX_FOUND) continue;

                            bool alreadyFound = false;
                            for (int j = 0; j < *foundCount; j++) {
                                if (foundUIDWidgets[j] == uiItem) {
                                    alreadyFound = true;
                                    break;
                                }
                            }

                            if (!alreadyFound && *foundCount < MAX_FOUND) {
                                char buf[256];
                                sprintf_s(buf, "[*** UID ***] %ls\n", nameBuf);
                                //OutputDebugStringA(buf);
                                foundUIDWidgets[(*foundCount)++] = uiItem;
                            }
                            continue;
                        }

                        UUIItem** children = nullptr;
                        int childCount = 0;

                        if (SEH_GetChildren(uiItem, &children, &childCount) && children) {
                            for (int c = 0; c < childCount; c++) {
                                wchar_t childName[256] = { 0 };
                                if (SEH_GetDisplayName(children[c], childName, 256)) {
                                    if (debugMode && (*totalDumpedCount < 2000)) {
                                        char buf[512];
                                        sprintf_s(buf, "[%d] %ls\n", (*totalDumpedCount)++, childName);
                                        //OutputDebugStringA(buf);
                                    }

                                    if (IsUIDWidgetName(childName)) {
                                        if (*foundCount >= MAX_FOUND) break;

                                        bool alreadyFound = false;
                                        for (int j = 0; j < *foundCount; j++) {
                                            if (foundUIDWidgets[j] == children[c]) {
                                                alreadyFound = true;
                                                break;
                                            }
                                        }

                                        if (!alreadyFound && *foundCount < MAX_FOUND) {
                                            char buf[256];
                                            sprintf_s(buf, "[*** UID ***] %ls\n", childName);
                                            //OutputDebugStringA(buf);
                                            foundUIDWidgets[(*foundCount)++] = children[c];
                                        }
                                    }
                                }
                            }

                            free(children);
                        }
                    }
                }
            }

            TArray<UActorComponent*> components = actor->K2_GetComponentsByClass(UUIItem::StaticClass());

            for (int j = 0; j < components.Num(); j++) {
                UUIItem* uiItem = static_cast<UUIItem*>(components[j]);

                wchar_t nameBuf[256] = { 0 };
                if (SEH_GetDisplayName(uiItem, nameBuf, 256)) {
                    if (debugMode && (*totalDumpedCount < 2000)) {
                        char buf[512];
                        sprintf_s(buf, "[%d] %ls\n", (*totalDumpedCount)++, nameBuf);
                        //OutputDebugStringA(buf);
                    }

                    if (IsUIDWidgetName(nameBuf)) {
                        if (*foundCount >= MAX_FOUND) continue;

                        bool alreadyFound = false;
                        for (int k = 0; k < *foundCount; k++) {
                            if (foundUIDWidgets[k] == uiItem) {
                                alreadyFound = true;
                                break;
                            }
                        }

                        if (!alreadyFound && *foundCount < MAX_FOUND) {
                            char buf[256];
                            sprintf_s(buf, "[*** UID ***] %ls\n", nameBuf);
                            //OutputDebugStringA(buf);
                            foundUIDWidgets[(*foundCount)++] = uiItem;
                        }
                        continue;
                    }

                    UUIItem** children = nullptr;
                    int childCount = 0;

                    if (SEH_GetChildren(uiItem, &children, &childCount) && children) {
                        for (int c = 0; c < childCount; c++) {
                            wchar_t childName[256] = { 0 };
                            if (SEH_GetDisplayName(children[c], childName, 256)) {
                                if (debugMode && (*totalDumpedCount < 2000)) {
                                    char buf[512];
                                    sprintf_s(buf, "[%d] %ls\n", (*totalDumpedCount)++, childName);
                                    //OutputDebugStringA(buf);
                                }

                                if (IsUIDWidgetName(childName)) {
                                    if (*foundCount >= MAX_FOUND) break;

                                    bool alreadyFound = false;
                                    for (int k = 0; k < *foundCount; k++) {
                                        if (foundUIDWidgets[k] == children[c]) {
                                            alreadyFound = true;
                                            break;
                                        }
                                    }

                                    if (!alreadyFound && *foundCount < MAX_FOUND) {
                                        char buf[256];
                                        sprintf_s(buf, "[*** UID ***] %ls\n", childName);
                                        //OutputDebugStringA(buf);
                                        foundUIDWidgets[(*foundCount)++] = children[c];
                                    }
                                }
                            }
                        }

                        free(children);
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

}
