using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

namespace ww_unlockfps
{
    /// <summary>
    /// Advancelaunch.xaml 的交互逻辑
    /// </summary>
    public partial class Advancelaunch : Window
    {
        private CancellationTokenSource _cancellationTokenSource;

        // 添加修改标签内容的方法
        public void UpdateTipMessage(string message)
        {
             tiplaunch.Content = message;
             tiplaunch.Foreground = Brushes.Green;
            // 可选：同时更新其他UI状态
            // tiplaunch.Foreground = Brushes.Green;
        }



        public Advancelaunch()
        {
            InitializeComponent();
            //_cancellationTokenSource = new CancellationTokenSource();
            //Task.Run(() => MonitorProcess(_cancellationTokenSource.Token), _cancellationTokenSource.Token);
        }

        //private async Task MonitorProcess(CancellationToken cancellationToken)
        //{
        //    while (!cancellationToken.IsCancellationRequested)
        //    {
        //        try
        //        {
        //            string processNametest = "Client-Win64-Shipping";
        //            if (IsProcessLaunch(processNametest))
        //            {
        //                Console.WriteLine($"{processNametest} is running");
        //                break;
        //            }
        //            else
        //            {
        //                Console.WriteLine($"{processNametest} NOT launch");
        //            }
        //        }
        //        catch (Exception ex)
        //        {
        //            // 记录异常
        //            Console.WriteLine($"Exception: {ex.Message}");
        //        }

        //        try
        //        {
        //            // 使用 Task.Delay 代替 Thread.Sleep，避免忙等待
        //            await Task.Delay(1000, cancellationToken);
        //        }
        //        catch (TaskCanceledException)
        //        {
        //            // 任务被取消时退出循环
        //            break;
        //        }

        //        //Console.WriteLine("try to hook ww game");
        //    }
        //}

        private async Task MonitorProcess(CancellationToken cancellationToken)
        {
            string processNametest = "Client-Win64-Shipping";
            Process process = null;

            while (!cancellationToken.IsCancellationRequested)
            {
                try
                {
                    // 如果进程未初始化或已退出，重新获取进程
                    if (process == null || process.HasExited)
                    {
                        Process[] processes = Process.GetProcessesByName(processNametest);
                        if (processes.Length > 0)
                        {
                            process = processes[0];
                            Console.WriteLine($"{processNametest} is running");
                        }
                        else
                        {
                            Console.WriteLine($"{processNametest} NOT launch");
                            process = null; // 重置进程对象
                        }
                    }

                    // 如果进程已初始化且未退出，检查主窗口句柄
                    if (process != null && !process.HasExited)
                    {
                        if (process.MainWindowHandle != IntPtr.Zero)
                        {
                            Console.WriteLine($"{processNametest} has a valid main window handle");
                            goto startunlockww;
                        }
                        else
                        {
                            Console.WriteLine($"{processNametest} does NOT have a valid main window handle");
                        }
                    }
                }
                catch (Exception ex)
                {
                    // 记录异常
                    Console.WriteLine($"Exception: {ex.Message}");
                    process = null; // 重置进程对象
                }

                try
                {
                    // 使用 Task.Delay 代替 Thread.Sleep，避免忙等待
                    await Task.Delay(1000, cancellationToken);
                }
                catch (TaskCanceledException)
                {
                    // 任务被取消时退出循环
                    break;
                }
            }

            startunlockww:
            {
                const int PROCESS_QUERY_INFORMATION = 0x0400;
                const int PROCESS_VM_READ = 0x0010;
                const int PROCESS_VM_WRITE = 0x0020;
                const int PROCESS_VM_OPERATION = 0x0008;

                [DllImport("kernel32.dll")]
                static extern IntPtr OpenProcess(int dwDesiredAccess, bool bInheritHandle, int dwProcessId);

                [DllImport("kernel32.dll")]
                static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);

                [DllImport("kernel32.dll")]
                static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesWritten);

                [DllImport("user32.dll")]
                static extern IntPtr GetForegroundWindow();

                [DllImport("user32.dll")]
                static extern int GetWindowThreadProcessId(IntPtr hWnd, out int lpdwProcessId);

                if (process == null)
                {
                    Console.WriteLine("Process not found.");
                    return;
                }

                IntPtr processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, false, process.Id);
                IntPtr moduleBaseAddress = process.MainModule.BaseAddress + 0x08618588;
                int rootAddress;
                byte[] rootBuffer = new byte[4];
                ReadProcessMemory(processHandle, moduleBaseAddress, rootBuffer, rootBuffer.Length, out IntPtr bytesRead);
                rootAddress = BitConverter.ToInt32(rootBuffer, 0);

                float newValue=1500.0f;

                while (true)
                { 
                    byte[] newValueBuffer = BitConverter.GetBytes(newValue);
                    WriteProcessMemory(processHandle, (IntPtr)(rootAddress + 0x0), newValueBuffer, newValueBuffer.Length, out IntPtr bytesWritten);
                    Thread.Sleep(300);
                }
            }
        }








        private bool IsProcessLaunch(string processNametest)
        {
            Process[] processes = Process.GetProcessesByName(processNametest);
            return processes.Length > 0;
        }

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            base.OnClosing(e);
            //_cancellationTokenSource.Cancel();
        }

        private void Hyperlink_RequestNavigate(object sender, RequestNavigateEventArgs e)
        {
            // 使用 Process.Start 来打开默认浏览器
            Process.Start(new ProcessStartInfo(e.Uri.AbsoluteUri) { UseShellExecute = true });
            e.Handled = true; // 标记事件已处理
        }
    }
}
