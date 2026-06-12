using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.IO;

namespace ww_unlockfps.Utils
{
    internal class Natives
    {
        [DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern int GetWindowThreadProcessId(IntPtr hWnd, out int lpdwProcessId);

        [DllImport("user32.dll")]
        private static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder lpClassName, int nMaxCount);

        private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        public static bool IsUnrealWindowRunning(string processName)
        {
            Process[] processes = Process.GetProcessesByName(processName);
            if (processes.Length == 0) return false;

            foreach (var process in processes)
            {
                if (process.HasExited) continue;

                int targetPid = process.Id;
                bool foundUnrealWindow = false;

                // 遍历所有窗口
                EnumWindows((hWnd, lParam) =>
                {
                    GetWindowThreadProcessId(hWnd, out int windowPid);

                    // 如果窗口属于当前检查的进程
                    if (windowPid == targetPid)
                    {
                        // 获取窗口类名
                        System.Text.StringBuilder className = new System.Text.StringBuilder(256);
                        GetClassName(hWnd, className, className.Capacity);

                        // 严谨判断：类名是否等于 "UnrealWindow"
                        if (className.ToString() == "UnrealWindow")
                        {
                            foundUnrealWindow = true;
                            return false; // 找到了，停止遍历
                        }
                    }
                    return true; // 继续遍历
                }, IntPtr.Zero);

                if (foundUnrealWindow)
                {
                    return true;
                }
            }

            return false;
        }



        [Flags]
        public enum CreateProcessFlags : uint
        {
            CREATE_SUSPENDED = 0x00000004,
            CREATE_NEW_CONSOLE = 0x00000010,
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct StartupInfo
        {
            public int cb;
            public string lpReserved;
            public string lpDesktop;
            public string lpTitle;
            public uint dwX;
            public uint dwY;
            public uint dwXSize;
            public uint dwYSize;
            public uint dwXCountChars;
            public uint dwYCountChars;
            public uint dwFillAttribute;
            public uint dwFlags;
            public short wShowWindow;
            public short cbReserved2;
            public IntPtr lpReserved2;
            public IntPtr hStdInput;
            public IntPtr hStdOutput;
            public IntPtr hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ProcessInformation
        {
            public IntPtr hProcess;
            public IntPtr hThread;
            public uint dwProcessId;
            public uint dwThreadId;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool CreateProcess(
            string lpApplicationName,
            string lpCommandLine,
            IntPtr lpProcessAttributes,
            IntPtr lpThreadAttributes,
            bool bInheritHandles,
            CreateProcessFlags dwCreationFlags,
            IntPtr lpEnvironment,
            string lpCurrentDirectory,
            ref StartupInfo lpStartupInfo,
            out ProcessInformation lpProcessInformation);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr VirtualAllocEx(
            IntPtr hProcess,
            IntPtr lpAddress,
            uint dwSize,
            uint flAllocationType,
            uint flProtect);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool WriteProcessMemory(
            IntPtr hProcess,
            IntPtr lpBaseAddress,
            byte[] lpBuffer,
            uint nSize,
            out IntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
        public static extern IntPtr GetProcAddress(
            IntPtr hModule,
            string lpProcName);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr LoadLibrary(string lpFileName);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr GetModuleHandle(string lpModuleName);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr CreateRemoteThread(
            IntPtr hProcess,
            IntPtr lpThreadAttributes,
            uint dwStackSize,
            IntPtr lpStartAddress,
            IntPtr lpParameter,
            uint dwCreationFlags,
            out IntPtr lpThreadId);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint ResumeThread(IntPtr hThread);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeThread(IntPtr hThread, out uint lpExitCode);

        //public static bool StartAndInject(string exePath, string arguments, string dllPath, bool shouldInject)
        public static bool StartAndInject(string exePath, string arguments, List<string> dllPaths, bool shouldInject)
        {
            StartupInfo si = new StartupInfo();
            si.cb = Marshal.SizeOf(si);

            ProcessInformation pi = new ProcessInformation();

            string commandLine = $"\"{exePath}\" {arguments}".Trim();

            // 创建挂起进程
            bool success = CreateProcess(
                null,
                commandLine,
                IntPtr.Zero,
                IntPtr.Zero,
                false,
                CreateProcessFlags.CREATE_SUSPENDED,
                IntPtr.Zero,
                null,
                ref si,
                out pi);

            if (!success)
            {
                Console.WriteLine($"创建进程失败: {Marshal.GetLastWin32Error()}");
                return false;
            }

            try
            {
                // 不注入直接启动
                if (!shouldInject)
                {
                    ResumeThread(pi.hThread);
                    Console.WriteLine("已启动（未注入）");
                    return true;
                }

                IntPtr hKernel32 = GetModuleHandle("kernel32.dll");

                if (hKernel32 == IntPtr.Zero)
                    hKernel32 = LoadLibrary("kernel32.dll");

                if (hKernel32 == IntPtr.Zero)
                {
                    Console.WriteLine($"获取 kernel32.dll 失败: {Marshal.GetLastWin32Error()}");
                    return false;
                }

                IntPtr loadLibraryAddr = GetProcAddress(hKernel32, "LoadLibraryW");

                if (loadLibraryAddr == IntPtr.Zero)
                {
                    Console.WriteLine($"GetProcAddress 失败: {Marshal.GetLastWin32Error()}");
                    return false;
                }

                foreach (string dllPath in dllPaths)
                {
                    byte[] dllBytes = Encoding.Unicode.GetBytes(dllPath + "\0");

                    IntPtr allocAddr = VirtualAllocEx(
                        pi.hProcess,
                        IntPtr.Zero,
                        (uint)dllBytes.Length,
                        0x3000,
                        0x04);

                    if (allocAddr == IntPtr.Zero)
                    {
                        Console.WriteLine($"VirtualAllocEx失败: {Marshal.GetLastWin32Error()}");
                        return false;
                    }

                    if (!WriteProcessMemory(
                            pi.hProcess,
                            allocAddr,
                            dllBytes,
                            (uint)dllBytes.Length,
                            out _))
                    {
                        Console.WriteLine($"WriteProcessMemory失败: {Marshal.GetLastWin32Error()}");
                        return false;
                    }

                    IntPtr threadId;

                    IntPtr hThread = CreateRemoteThread(
                        pi.hProcess,
                        IntPtr.Zero,
                        0,
                        loadLibraryAddr,
                        allocAddr,
                        0,
                        out threadId);

                    if (hThread == IntPtr.Zero)
                    {
                        Console.WriteLine($"CreateRemoteThread失败: {Marshal.GetLastWin32Error()}");
                        return false;
                    }

                    CloseHandle(hThread);

                    Console.WriteLine($"注入成功: {dllPath}");
                }

                // 所有 DLL 注入完成后恢复主线程
                ResumeThread(pi.hThread);

                Console.WriteLine("全部DLL注入成功");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"异常: {ex.Message}");
                return false;
            }
            finally
            {
                if (pi.hProcess != IntPtr.Zero)
                    CloseHandle(pi.hProcess);

                if (pi.hThread != IntPtr.Zero)
                    CloseHandle(pi.hThread);
            }
        }

        public static bool InjectOnly(string processName, List<string> dllPaths)
        {
            Process[] processes = Process.GetProcessesByName(processName);

            if (processes.Length == 0)
            {
                Console.WriteLine("未找到目标进程");
                return false;
            }

            // 默认取第一个
            Process target = processes[0];

            IntPtr hProcess = target.Handle;

            try
            {
                // 获取 LoadLibraryW 地址（只获取一次）
                IntPtr hKernel32 = GetModuleHandle("kernel32.dll");

                if (hKernel32 == IntPtr.Zero)
                {
                    hKernel32 = LoadLibrary("kernel32.dll");
                }

                if (hKernel32 == IntPtr.Zero)
                {
                    Console.WriteLine("获取kernel32失败");
                    return false;
                }

                IntPtr loadLibraryAddr = GetProcAddress(hKernel32, "LoadLibraryW");

                if (loadLibraryAddr == IntPtr.Zero)
                {
                    Console.WriteLine($"GetProcAddress失败: {Marshal.GetLastWin32Error()}");
                    return false;
                }

                // 逐个DLL注入
                foreach (string dllPath in dllPaths)
                {
                    if (!File.Exists(dllPath))
                    {
                        Console.WriteLine($"DLL不存在: {dllPath}");
                        return false;
                    }

                    byte[] dllBytes = Encoding.Unicode.GetBytes(dllPath + "\0");

                    IntPtr allocAddr = VirtualAllocEx(
                        hProcess,
                        IntPtr.Zero,
                        (uint)dllBytes.Length,
                        0x3000, // MEM_COMMIT | MEM_RESERVE
                        0x04);  // PAGE_READWRITE

                    if (allocAddr == IntPtr.Zero)
                    {
                        Console.WriteLine($"VirtualAllocEx失败: {Marshal.GetLastWin32Error()}");
                        return false;
                    }

                    if (!WriteProcessMemory(
                            hProcess,
                            allocAddr,
                            dllBytes,
                            (uint)dllBytes.Length,
                            out _))
                    {
                        Console.WriteLine($"WriteProcessMemory失败: {Marshal.GetLastWin32Error()}");
                        return false;
                    }

                    IntPtr threadId;

                    IntPtr hThread = CreateRemoteThread(hProcess, IntPtr.Zero, 0, loadLibraryAddr, allocAddr, 0, out threadId);

                    if (hThread == IntPtr.Zero)
                    {
                        Console.WriteLine($"CreateRemoteThread失败: {Marshal.GetLastWin32Error()}");
                        return false;
                    }

                    CloseHandle(hThread);

                    Console.WriteLine($"注入成功: {dllPath}");
                }

                Console.WriteLine("全部DLL注入成功");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"异常: {ex.Message}");
                return false;
            }
        }

        //public static bool InjectOnly(string processName, List<string> dllPaths)
        //{
        //    Process[] processes = Process.GetProcessesByName(processName);

        //    if (processes.Length == 0)
        //    {
        //        Console.WriteLine("未找到目标进程");
        //        return false;
        //    }

        //    // 默认取第一个
        //    Process target = processes[0];

        //    IntPtr hProcess = target.Handle;

        //    try
        //    {
        //        // 1️⃣ 写入 DLL 路径
        //        byte[] dllBytes = Encoding.Unicode.GetBytes(dllPath + "\0");

        //        IntPtr allocAddr = VirtualAllocEx(
        //            hProcess,
        //            IntPtr.Zero,
        //            (uint)dllBytes.Length,
        //            0x3000,
        //            0x04);

        //        if (allocAddr == IntPtr.Zero)
        //        {
        //            Console.WriteLine($"VirtualAllocEx失败: {Marshal.GetLastWin32Error()}");
        //            return false;
        //        }

        //        if (!WriteProcessMemory(hProcess, allocAddr, dllBytes, (uint)dllBytes.Length, out _))
        //        {
        //            Console.WriteLine($"WriteProcessMemory失败: {Marshal.GetLastWin32Error()}");
        //            return false;
        //        }

        //        // 2️⃣ 获取 LoadLibraryW 地址
        //        IntPtr hKernel32 = GetModuleHandle("kernel32.dll");
        //        if (hKernel32 == IntPtr.Zero)
        //        {
        //            hKernel32 = LoadLibrary("kernel32.dll");
        //        }

        //        if (hKernel32 == IntPtr.Zero)
        //        {
        //            Console.WriteLine("获取kernel32失败");
        //            return false;
        //        }

        //        IntPtr loadLibraryAddr = GetProcAddress(hKernel32, "LoadLibraryW");

        //        if (loadLibraryAddr == IntPtr.Zero)
        //        {
        //            Console.WriteLine($"GetProcAddress失败: {Marshal.GetLastWin32Error()}");
        //            return false;
        //        }

        //        // 3️⃣ 创建远程线程
        //        IntPtr threadId;
        //        IntPtr hThread = CreateRemoteThread(
        //            hProcess,
        //            IntPtr.Zero,
        //            0,
        //            loadLibraryAddr,
        //            allocAddr,
        //            0,
        //            out threadId);

        //        if (hThread == IntPtr.Zero)
        //        {
        //            Console.WriteLine($"CreateRemoteThread失败: {Marshal.GetLastWin32Error()}");
        //            return false;
        //        }

        //        CloseHandle(hThread);

        //        Console.WriteLine("✅ 注入成功（未启动进程）");
        //        return true;
        //    }
        //    catch (Exception ex)
        //    {
        //        Console.WriteLine($"异常: {ex.Message}");
        //        return false;
        //    }
        //}
    }
}
