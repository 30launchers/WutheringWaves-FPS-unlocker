#define _CRT_SECURE_NO_WARNINGS

#ifndef PCH_H
#define PCH_H
#include <string>
#include <iostream>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <vector>
#include <fstream>
#include <Xinput.h>
#include <cstdint>
#include <cstddef>

#include "PatternScanner.hpp"
#include "HookUtility.h"
#include "MinHookManager.h"
#include "MinHook/include/MinHook.h"


#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "Xinput.lib")

#endif
