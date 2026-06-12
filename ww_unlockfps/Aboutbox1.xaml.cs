using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Navigation;


namespace ww_unlockfps
{
    /// <summary>
    /// Aboutbox1.xaml 的交互逻辑
    /// </summary>
    public partial class Aboutbox1 : Window
    {
        public Aboutbox1()
        {
            InitializeComponent();
        }

        protected override void OnSourceInitialized(EventArgs e)
        {
            IconHelper.RemoveIcon(this);
        }
        public static class IconHelper
        {
            [DllImport("user32.dll")]
            static extern int GetWindowLong(IntPtr hwnd, int index);

            [DllImport("user32.dll")]
            static extern int SetWindowLong(IntPtr hwnd, int index, int newStyle);

            [DllImport("user32.dll")]
            static extern bool SetWindowPos(IntPtr hwnd, IntPtr hwndInsertAfter,
                       int x, int y, int width, int height, uint flags);

            [DllImport("user32.dll")]
            static extern IntPtr SendMessage(IntPtr hwnd, uint msg,
                       IntPtr wParam, IntPtr lParam);

            const int GWL_EXSTYLE = -20;
            const int WS_EX_DLGMODALFRAME = 0x0001;
            const int SWP_NOSIZE = 0x0001;
            const int SWP_NOMOVE = 0x0002;
            const int SWP_NOZORDER = 0x0004;
            const int SWP_FRAMECHANGED = 0x0020;
            const uint WM_SETICON = 0x0080;

            public static void RemoveIcon(Window window)
            {
                IntPtr hwnd = new WindowInteropHelper(window).Handle;

                int extendedStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
                SetWindowLong(hwnd, GWL_EXSTYLE, extendedStyle | WS_EX_DLGMODALFRAME);

                SetWindowPos(hwnd, IntPtr.Zero, 0, 0, 0, 0, SWP_NOMOVE |
                      SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
        }

        //private void ClickableText_MouseEnter(object sender, MouseEventArgs e)
        //{
        //    ClickableText.Foreground = Brushes.Red; 
        //}

        //private void ClickableText_MouseLeave(object sender, MouseEventArgs e)
        //{
        //    if (ClickableText.Foreground != Brushes.Yellow)
        //    {
        //        ClickableText.Foreground = Brushes.Blue;
        //    }
        //}

        private void ClickableText_MouseDown(object sender, MouseButtonEventArgs e)
        {
            //ClickableText.Foreground = Brushes.Red; // click change to red
            Process.Start(new ProcessStartInfo("https://space.bilibili.com/456492426") { UseShellExecute = true });
        }

        private void Hyperlink_RequestNavigate(object sender, RequestNavigateEventArgs e)
        {
            // 使用 Process.Start 来打开默认浏览器
            Process.Start(new ProcessStartInfo(e.Uri.AbsoluteUri) { UseShellExecute = true });
            e.Handled = true; // 标记事件已处理
        }
    }
}
