using IniParser;
using IniParser.Model;
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Forms;
using System.Windows.Input;
using System.Windows.Threading;
using Windows.Gaming.Preview.GamesEnumeration;
using Windows.Media.Protection.PlayReady;
using static System.Net.Mime.MediaTypeNames;
using static System.Windows.Forms.VisualStyles.VisualStyleElement;
using static ww_unlockfps.Utils.Natives;
using Application = System.Windows.Application;
using MessageBox = System.Windows.MessageBox;



namespace ww_unlockfps
{
    public partial class MainWindow : System.Windows.Window
    {
        //250327
        [DllImport("psapi.dll", SetLastError = true)]
        static extern bool EnumProcessModules(IntPtr hProcess, [Out] IntPtr[] lphModule, uint cb, out uint lpcbNeeded);

        [DllImport("psapi.dll", CharSet = CharSet.Auto)]
        static extern uint GetModuleFileNameEx(IntPtr hProcess, IntPtr hModule, StringBuilder lpBaseName, uint nSize);

        [DllImport("psapi.dll")]
        static extern bool GetModuleInformation(IntPtr hProcess, IntPtr hModule, out MODULEINFO lpmodinfo, uint cb);

        public NamedPipeClientStream pipeClient { get; private set; }

        public NamedPipeClientStream pipeClientFPS { get; private set; }

        [StructLayout(LayoutKind.Sequential)]
        public struct MODULEINFO
        {
            public IntPtr lpBaseOfDll;
            public uint SizeOfImage;
            public IntPtr EntryPoint;
        }

        //Origin code
        DispatcherTimer holdTimer;
        DispatcherTimer holdTimer_I;
        Task outputTask;
        Task outputTask_I;
        CancellationTokenSource cts;
        CancellationTokenSource cts_I;
        private static Mutex lau_mutex;
        private NotifyIcon notifyIcon;
        private const string ConfigFileName = "ww_fps_config.ini";
        private const string Section = "Settings";
        private const string InputKey = "FpsValue";
        private const string InputKey2 = "PathValue";
        private const string OptionKey = "DX11Enabled";
        private const string OptionKey2 = "AutoStartEnabled";
        private const string OptionKey3 = "PowerSavingEnabled";
        private const string OptionKey4 = "ProcessPriorityMode";
        private const string OptionKey5 = "GameServerArea";
        private const string OptionKey6 = "GameParam";
        private const string OptionKey7 = "GameLaunchExe";
        private const string OptionKey8 = "AdvanEnabled";
        private const string OptionKey9 = "FovEnabled";
        private const string OptionKey10 = "HideUidEnabled";
        private const string OptionKey11 = "FovValue";

        private bool ShouldCenterWindow { get; set; }
        private bool checkautostart = false;
        private bool startgame_test1 = true;
        private volatile float _newValue;
        private readonly object _lock = new object();

        private volatile int ck_priority;
        private readonly object _lock2 = new object();

        private volatile bool check_power_saving_sre = false;
        private readonly object _lock3 = new object();

        private volatile bool tagck2 = false;
        //private readonly object _lock4 = new object();
        bool isShutdownCompleted = false;
        //bool isunlockFPSCompleted = false;
        bool isunlockFPSErrorMSG = false;

        private int GameServerAvalue = -1;
        private int GameServerAresult = -1;


        public MainWindow()
        {
            InitializeComponent();
            DataContext = this;
            InitializeHoldTimer();
            InitializeHoldTimer_I();
            InitializeNotifyIcon();
            LoadConfig();
            bool createdNew;
            lau_mutex = new Mutex(true, "30launcher_WPF_WutheringWavesFPSunlocker", out createdNew);
            this.Activated += MainWindow_Activated;
            this.Deactivated += MainWindow_Deactivated;

            // 解压DLL文件 260507
            try
            {
                string path = LoadGenshinToolsDLL("ww_plugin.dll", "ww_ulk_plugin.dll");
                string pathfpsplugin = LoadGenshinToolsDLL("ww_plugin_base.dll", "ww_ulk_plugin_base.dll");
            }
            catch (Exception ex)
            {
                //Console.WriteLine($"加载DLL失败: {ex}");
                //string message = ex.Message;
                //System.Windows.MessageBox.Show(message, "Error:", MessageBoxButton.OK, MessageBoxImage.Error);
            }


            if (!createdNew)
            {
                MessageBox.Show("Another unlocker is already running", "Notification", MessageBoxButton.OK, MessageBoxImage.Warning);
                Application.Current.Shutdown();
            }
            else
            {
                if (check_auto_start.IsChecked == true)
                {
                    Start_Game(null, new RoutedEventArgs());
                    checkautostart = true;
                    if (startgame_test1 == true)
                    {
                        // 订阅窗口状态改变的事件
                        this.StateChanged += MainWindow_StateChanged;
                        this.ShouldCenterWindow = true;
                        Thread.Sleep(15);
                        this.Hide(); // Hide the window and show the tray icon
                        notifyIcon.BalloonTipIcon = ToolTipIcon.Info;
                        notifyIcon.BalloonTipTitle = "Wuther FPS Unlocker";
                        notifyIcon.BalloonTipText = "Minimized to tray";
                        notifyIcon.ShowBalloonTip(3000);
                    }
                }
            }
        }


        private void Priority_Click(object sender, RoutedEventArgs e)
        {

            // Uncheck all priority menu items
            foreach (var item in check_game_process_priority.Items)
            {
                if (item is MenuItem menuItem)
                {
                    menuItem.IsChecked = false;
                }
            }

            // Check the clicked menu item
            MenuItem clickedItem = sender as MenuItem;
            if (clickedItem != null)
            {
                clickedItem.IsChecked = true;
            }

            if (ck_realtime.IsChecked == true)
            {
                ck_priority = 6;
            }

            if (ck_high.IsChecked == true)
            {
                ck_priority = 5;
            }

            if (ck_above_normal.IsChecked == true)
            {
                ck_priority = 4;
            }

            if (ck_normal.IsChecked == true)
            {
                ck_priority = 3;
            }

            if (ck_below_normal.IsChecked == true)
            {
                ck_priority = 2;
            }

            if (ck_low.IsChecked == true)
            {
                ck_priority = 1;
            }

            if (ck_default.IsChecked == true)
            {
                ck_priority = 0;
            }

            //bool newcheck_power_saving_sre;
            //lock (_lock3)
            //{
            //    newcheck_power_saving_sre = check_power_saving_sre;
            //}

            //if (newcheck_power_saving_sre == true)
            //{
            //    MessageBox.Show("invivld", "Notification", MessageBoxButton.OK, MessageBoxImage.Information);
            //}
            //Console.WriteLine(ck_priority+"  firstcheck is");
        }



        private void Tty()
        {
            while (true)
            {

            }
        }

        private void MainWindow_StateChanged(object sender, EventArgs e)
        {
            // 如果窗口应该居中并且当前状态是正常状态
            if (this.ShouldCenterWindow && this.WindowState == WindowState.Normal)
            {
                CenterWindow();
                this.ShouldCenterWindow = false; // 居中后，重置标志，防止再次居中
            }
        }


        private void CenterWindow()
        {
            // 获取屏幕宽度和高度
            double screenWidth = System.Windows.SystemParameters.PrimaryScreenWidth;
            double screenHeight = System.Windows.SystemParameters.PrimaryScreenHeight;

            // 获取窗口宽度和高度
            double windowWidth = this.Width;
            double windowHeight = this.Height;

            // 计算新的窗口位置
            double left = (screenWidth / 2) - (windowWidth / 2);
            double top = (screenHeight / 2) - (windowHeight / 2);

            // 设置窗口位置
            this.Left = left;
            this.Top = top;
        }

        private void Power_Saving_Checked(object sender, RoutedEventArgs e)
        {
            //MessageBox.Show("Failed to load config file" + Environment.NewLine + "Your config file doesn't appear to be in the correct format" + Environment.NewLine + "It will be reset to default", "Notification", MessageBoxButton.OK, MessageBoxImage.Warning);
            lock (_lock3)
            {
                check_power_saving_sre = true;
            }
            tagck2 = false;
        }

        private void Power_Saving_Unchecked(object sender, RoutedEventArgs e)
        {
            //MessageBox.Show("Failed to load config file" + Environment.NewLine + "Your config file doesn't appear to be in the correct format" + Environment.NewLine + "It will be reset to default", "Notification", MessageBoxButton.OK, MessageBoxImage.Warning);
            lock (_lock3)
            {
                check_power_saving_sre = false;
            }





        }

        private void AutoStart_Checked(object sender, RoutedEventArgs e)
        {
            //MessageBox.Show("Failed to load config file" + Environment.NewLine + "Your config file doesn't appear to be in the correct format" + Environment.NewLine + "It will be reset to default", "Notification", MessageBoxButton.OK, MessageBoxImage.Warning);
        }

        private void AutoStart_Unchecked(object sender, RoutedEventArgs e)
        {
            //MessageBox.Show("Failed to load config file" + Environment.NewLine + "Your config file doesn't appear to be in the correct format" + Environment.NewLine + "It will be reset to default", "Notification", MessageBoxButton.OK, MessageBoxImage.Warning);
        }






        private void MainWindow_Activated(object sender, EventArgs e)
        {
            try
            {
                int value = int.Parse(Tb_main.Text);
                sli_main.Value = value;
            }
            catch (FormatException)
            {

            }
            catch (OverflowException)
            {

            }
        }

        private void MainWindow_Deactivated(object sender, EventArgs e)
        {
            try
            {
                int value = int.Parse(Tb_main.Text);
                sli_main.Value = value;
            }
            catch (FormatException)
            {

            }
            catch (OverflowException)
            {

            }
        }


        private void InitializeNotifyIcon()
        {
            notifyIcon = new NotifyIcon();
            //notifyIcon.Text = "unlocker" + Tb_main.Text;
            //notifyIcon.Icon = new System.Drawing.Icon("Resources/logo.ico"); 
            notifyIcon.Icon = System.Drawing.Icon.ExtractAssociatedIcon(System.Windows.Forms.Application.ExecutablePath);
            notifyIcon.Visible = true;

            // Context menu
            ContextMenuStrip contextMenu = new ContextMenuStrip();
            ToolStripMenuItem exitMenuItem = new ToolStripMenuItem("Quit");
            exitMenuItem.Click += ExitMenuItem_Click;
            contextMenu.Items.Add(exitMenuItem);
            notifyIcon.ContextMenuStrip = contextMenu;

            // Handle the DoubleClick event to show the window when the user double clicks the NotifyIcon
            notifyIcon.DoubleClick += NotifyIcon_DoubleClick;
        }

        //private void Tb_main_TextChanged(object sender, TextChangedEventArgs e)
        //{
        //    if (notifyIcon != null)
        //    {
        //        notifyIcon.Text = "WW unlocker (FPS: "+Tb_main.Text+")";
        //    }
        //}

        private void ExitMenuItem_Click(object sender, EventArgs e)
        {
            // Hide the notify icon and close the application
            notifyIcon.Visible = false;
            CleanupNotifyIcon();
            System.Windows.Application.Current.Shutdown();
        }

        private void NotifyIcon_DoubleClick(object sender, EventArgs e)
        {
            // Show the window
            this.Show();
            this.WindowState = WindowState.Normal;
        }

        protected override void OnStateChanged(EventArgs e)
        {
            if (WindowState == WindowState.Minimized)
            {
                this.Hide(); // Hide the window and show the tray icon
                notifyIcon.BalloonTipIcon = ToolTipIcon.Info;
                notifyIcon.BalloonTipTitle = "Wuther FPS Unlocker";
                notifyIcon.BalloonTipText = "Minimized to tray";
                notifyIcon.ShowBalloonTip(3000);
            }
            base.OnStateChanged(e);
        }


        // Method to clean up the NotifyIcon
        private void CleanupNotifyIcon()
        {
            if (notifyIcon != null)
            {
                notifyIcon.Visible = false;
                notifyIcon.Icon = null; // Set the icon to null to release the resource
                notifyIcon.Dispose(); // Call dispose to clean up the icon
                notifyIcon = null; // Set the variable to null to remove the reference
            }
        }



        public static class GlobalVariables
        {
            public static string Path_EXE_launch { get; set; }
        }


        private void Ck_Priority()
        {
            if (ck_priority == 6)
            {
                ck_realtime.IsChecked = true;
            }

            if (ck_priority == 5)
            {
                ck_high.IsChecked = true;
            }

            if (ck_priority == 4)
            {
                ck_above_normal.IsChecked = true;
            }

            if (ck_priority == 3)
            {
                ck_normal.IsChecked = true;
            }

            if (ck_priority == 2)
            {
                ck_below_normal.IsChecked = true;
            }

            if (ck_priority == 1)
            {
                ck_low.IsChecked = true;
            }

            if (ck_priority == 0)
            {
                ck_default.IsChecked = true;
            }
        }


        private void LoadConfig()
        {
            bool checkinisuccess = true;

            if (!File.Exists(ConfigFileName))
            {
                CreateConfigFile();
                MessageBox.Show("             ❄  叮~~~   Instructions  ❄" + Environment.NewLine + "" + Environment.NewLine + "1.Please set the Game EXE path at first" + Environment.NewLine +
                    "2.FPS unlocker will minimized to tray" + Environment.NewLine + "after game start"
                    + Environment.NewLine + "3.All settings will be saved automatically", "Welcome");
                //20241209
                checkautostart = true;
            }

            try
            {
                var parser = new FileIniDataParser();
                var data = parser.ReadFile(ConfigFileName);
                Tb_main.Text = data[Section][InputKey];
                check_auto_start.IsChecked = bool.Parse(data[Section][OptionKey2]);
                check_power_saving.IsChecked = bool.Parse(data[Section][OptionKey3]);
                check_dx11.IsChecked = bool.Parse(data[Section][OptionKey]);
                string value = data[Section][InputKey];
                if (double.TryParse(value, out double numericValue))
                {
                    sli_main.Value = numericValue;
                }
                else
                {
                    Tb_main.Text = ((int)(165)).ToString();
                    sli_main.Value = 165;
                }
                string ProcessPriority = data[Section][OptionKey4];
                int prV = int.Parse(ProcessPriority);
                ck_priority = prV;
                //Console.WriteLine(prV+"uuuu");

                if (prV != 6 && prV != 5 && prV != 4 && prV != 3 && prV != 2 && prV != 1 && prV != 0)
                {
                    ck_priority = 0;
                    //CreateConfigFile();
                    //MessageBox.Show("Failed to load config file" + Environment.NewLine + "Your config file doesn't appear to be in the correct format" + Environment.NewLine + "It will be reset to default", "Notification", MessageBoxButton.OK, MessageBoxImage.Warning);
                }
                Ck_Priority();

                string GameServerA = data[Section][OptionKey5];
                int GameServerAvalue1 = int.Parse(GameServerA);

                if (GameServerAvalue1 != 0 && GameServerAvalue1 != 1 && GameServerAvalue1 != 2)
                {
                    Console.WriteLine("area error");
                    data[Section][OptionKey5] = 2.ToString();
                    parser.WriteFile(ConfigFileName, data);
                }


                string GameParamA = data[Section][OptionKey6];
                if (GameParamA == null)
                {
                    Console.WriteLine("ERRORparam");
                    checkinisuccess = false;
                }
                //string GameParamAvalue1 = GameParamA.ToString();

                if (GameParamA == "")
                {
                    Console.WriteLine("当前启动参数为空");
                }
                else
                {
                    Console.WriteLine("当前启动参数" + GameParamA);
                }


                string GameLaunchExeA = data[Section][OptionKey7];
                int GameLaunchExeAvalue1 = int.Parse(GameLaunchExeA);
                if (GameLaunchExeAvalue1 != 0 && GameLaunchExeAvalue1 != 1)
                {
                    Console.WriteLine("GameLaunchExe error");
                    data[Section][OptionKey7] = 1.ToString();
                    parser.WriteFile(ConfigFileName, data);
                }

                cb_advanced.IsChecked = bool.Parse(data[Section][OptionKey8]);
                check_fov.IsChecked = bool.Parse(data[Section][OptionKey9]);
                check_hideuid.IsChecked = bool.Parse(data[Section][OptionKey10]);
                string FovValueStr = data[Section][OptionKey11];
                if (double.TryParse(FovValueStr, out double fovValue))
                {
                    Tb_fov.Value = fovValue;
                    sli_fov.Value = fovValue;
                }
                else
                {
                    Tb_fov.Value = 45;
                    sli_fov.Value = 45;
                }

            }
            catch (Exception)
            {
                CreateConfigFile();
                MessageBox.Show("Failed to load config file" + Environment.NewLine + "Your config file doesn't appear to be in the correct format" + Environment.NewLine + "It will be reset to default", "Notification", MessageBoxButton.OK, MessageBoxImage.Warning);
                //20241209
                checkautostart = true;
            }

            if (checkinisuccess == false)
            {
                CreateConfigFile();
                MessageBox.Show("Failed to load config file" + Environment.NewLine + "Your config file doesn't appear to be in the correct format" + Environment.NewLine + "It will be reset to default", "Notification", MessageBoxButton.OK, MessageBoxImage.Warning);
                //20241209
                checkautostart = true;
            }
        }





        private void CreateConfigFile()
        {
            var parser = new FileIniDataParser();
            var data = new IniData();
            data.Sections.AddSection(Section);
            data[Section][InputKey] = string.Empty;
            data[Section][InputKey2] = "Drag the Game EXE file onto the top";
            data[Section][OptionKey] = false.ToString();
            data[Section][OptionKey2] = false.ToString();
            data[Section][OptionKey3] = false.ToString();
            int DefaultPriorityMode = 0;
            data[Section][OptionKey4] = DefaultPriorityMode.ToString();
            data[Section][OptionKey5] = 2.ToString();
            data[Section][OptionKey6] = string.Empty;
            data[Section][OptionKey7] = 0.ToString();
            data[Section][OptionKey8] = false.ToString();
            data[Section][OptionKey9] = false.ToString();
            data[Section][OptionKey10] = false.ToString();
            data[Section][OptionKey11] = 45.ToString("F1");
            check_auto_start.IsChecked = false;
            check_power_saving.IsChecked = false;
            ck_default.IsChecked = true;
            cb_advanced.IsChecked = false;
            check_fov.IsChecked = false;
            check_hideuid.IsChecked = false;
            Tb_main.Text = ((int)(165)).ToString();
            sli_main.Value = 165;
            Tb_fov.Value = 45;
            sli_fov.Value = 45;
            parser.WriteFile(ConfigFileName, data);
        }

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            base.OnClosing(e);
            CleanupNotifyIcon();
            SaveConfig();
        }


        private void SaveConfig()
        {
            var parser = new FileIniDataParser();
            if (!File.Exists(ConfigFileName))
            {
                CreateConfigFile();
            }
            var data = parser.ReadFile(ConfigFileName);
            int slivaluetemp = (int)sli_main.Value;
            Tb_main.Text = slivaluetemp.ToString();
            data[Section][InputKey] = Tb_main.Text;
            data[Section][OptionKey] = check_dx11.IsChecked.ToString();
            data[Section][OptionKey2] = check_auto_start.IsChecked.ToString();
            data[Section][OptionKey3] = check_power_saving.IsChecked.ToString();
            data[Section][OptionKey4] = ck_priority.ToString();
            data[Section][OptionKey8] = cb_advanced.IsChecked.ToString();
            data[Section][OptionKey9] = check_fov.IsChecked.ToString();
            data[Section][OptionKey10] = check_hideuid.IsChecked.ToString();
            data[Section][OptionKey11] = sli_fov.Value.ToString("F1");
            parser.WriteFile(ConfigFileName, data);
        }


        private void TextBox_PreviewTextInput(object sender, TextCompositionEventArgs e)
        {
            int t = 0;
            if (e.Text == " ")
                e.Handled = true;
            if (!int.TryParse(e.Text, out t))
                //if (!int.TryParse(e.Text, out t) && e.Text != ".") //allow mount point
                e.Handled = true;
            System.Windows.Controls.TextBox textBox = sender as System.Windows.Controls.TextBox;

            // 修复textbox对象是null导致的异常 260614
            if (textBox == null)
                return;

            if (textBox.Text == "0" && textBox.SelectionStart == 1 && e.Text != ".")
            //if ((textBox.SelectionStart == 0 && e.Text == "0") || (textBox.Text == "0" && textBox.SelectionStart == 1 && e.Text != ".")) //not allowed first zero
            {
                e.Handled = true;
                return;
            }

            double currentValue;
            if (double.TryParse(((System.Windows.Controls.TextBox)sender).Text + e.Text, out currentValue))
            {
                if (currentValue < 0 || currentValue > 420)
                {
                    e.Handled = true;
                }
            }
        }

        private void TextBox_Pasting(object sender, DataObjectPastingEventArgs e)
        {
            if (e.DataObject.GetDataPresent(typeof(String)))
            {
                String text = (String)e.DataObject.GetData(typeof(String));
                double d = 0;
                if (!double.TryParse(text, out d))
                { e.CancelCommand(); }
            }
            else { e.CancelCommand(); }
        }


        private void TextBox_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
        {
            if (e.Key == Key.Space)
            {
                e.Handled = true;
            }
        }


        private void Exit_Main(object sender, RoutedEventArgs e)
        {
            this.Close();
        }

        private void Slider_ValueChanged_Main(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            Tb_main.Text = ((int)(sender as Slider).Value).ToString();
            float parsedValue = (float)sli_main.Value;
            lock (_lock)
            {
                _newValue = parsedValue;
            }

            Console.WriteLine(parsedValue);

            if (notifyIcon != null)
            {
                notifyIcon.Text = "WW unlocker (FPS: " + Tb_main.Text + ")";
            }
        }




        private void Button_Up_Click(object sender, RoutedEventArgs e)
        {
            //int maintextboxValue = int.Parse(Tb_main.Text);
            int maintextboxValue = (int)sli_main.Value;

            if (maintextboxValue < 420)
            {
                maintextboxValue++;
                Tb_main.Text = maintextboxValue.ToString();
                try
                {
                    sli_main.Value = maintextboxValue;
                }
                catch (FormatException)
                {

                }
                catch (OverflowException)
                {

                }
            }
            else
                MessageBox.Show("This is the maximum value", "Notification", MessageBoxButton.OK, MessageBoxImage.Information);

        }


        private void Button_Down_Click(object sender, RoutedEventArgs e)
        {
            //int maintextboxValue = int.Parse(Tb_main.Text);
            int maintextboxValue = (int)sli_main.Value;

            if (maintextboxValue > 0)
            {
                maintextboxValue--;
                Tb_main.Text = maintextboxValue.ToString();
                try
                {
                    sli_main.Value = maintextboxValue;
                }
                catch (FormatException)
                {

                }
                catch (OverflowException)
                {

                }
            }
            else
                MessageBox.Show("This is the minimum value", "Notification", MessageBoxButton.OK, MessageBoxImage.Information);
        }


        //decrese
        private void InitializeHoldTimer()
        {
            holdTimer = new DispatcherTimer();
            holdTimer.Interval = TimeSpan.FromSeconds(0.3);
            holdTimer.Tick += (s, e) =>
            {
                holdTimer.Stop();
                StartOutputLoop();
            };
        }

        private void StartOutputLoop()
        {
            cts = new CancellationTokenSource();
            outputTask = Task.Run(() =>
            {
                while (!cts.IsCancellationRequested)
                {
                    Dispatcher.Invoke(() =>
                    {
                        //int maintextboxValue = int.Parse(Tb_main.Text);
                        int maintextboxValue = (int)sli_main.Value;
                        if (maintextboxValue > 0)
                        {
                            Button_Down_Click(this, new RoutedEventArgs());
                        }
                    });
                    Thread.Sleep(15);
                }
            }, cts.Token);
        }

        private void btn_increase_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (!holdTimer.IsEnabled)
            {
                holdTimer.Start();
            }
        }

        private void btn_increase_PreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            holdTimer.Stop();
            if (cts != null)
            {
                cts.Cancel();
            }
            Button_Down_Click(sender, e);
        }

        //increase
        private void InitializeHoldTimer_I()
        {
            holdTimer_I = new DispatcherTimer();
            holdTimer_I.Interval = TimeSpan.FromSeconds(0.3);
            holdTimer_I.Tick += (s, e) =>
            {
                holdTimer_I.Stop();
                StartOutputLoop_I();
            };
        }

        private void StartOutputLoop_I()
        {
            cts_I = new CancellationTokenSource();
            outputTask_I = Task.Run(() =>
            {
                while (!cts_I.IsCancellationRequested)
                {
                    Dispatcher.Invoke(() =>
                    {
                        int maintextboxValue = (int)sli_main.Value;
                        //int maintextboxValue = int.Parse(Tb_main.Text);
                        if (maintextboxValue < 420)
                        {
                            Button_Up_Click(this, new RoutedEventArgs());
                        }
                    });
                    Thread.Sleep(15);
                }
            }, cts_I.Token);
        }

        private void btn_decrease_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (!holdTimer_I.IsEnabled)
            {
                holdTimer_I.Start();
            }
        }

        private void btn_decrease_PreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            holdTimer_I.Stop();
            if (cts_I != null)
            {
                cts_I.Cancel();
            }
            Button_Up_Click(sender, e);
        }


        private void About_Click(object sender, RoutedEventArgs e)
        {
            Aboutbox1 windowab = new Aboutbox1();

            // 设置模态窗口的Owner属性为主窗口
            windowab.Owner = this;
            // 保存主窗口的正常透明度
            double normal = this.Opacity;

            var parentTop = this.Top;
            var parentLeft = this.Left;
            var parentWidth = this.ActualWidth;
            var parentHeight = this.ActualHeight;
            double top = parentTop + (parentHeight - windowab.Height) / 2;
            double left = parentLeft + (parentWidth - windowab.Width) / 2;
            windowab.Top = top;
            windowab.Left = left;

            // 降低主窗口的透明度
            this.Opacity = 0.4;
            windowab.ShowDialog();
            // 模态窗口关闭后，恢复主窗口的正常透明度
            this.Opacity = normal;

        }

        private void ClearCache_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                if (pipeClient != null && pipeClient.IsConnected)
                {
                    int advanced = cb_advanced.IsChecked == true ? 1 : 0;
                    int enableFov = check_fov.IsChecked == true ? 1 : 0;
                    int hideUid = check_hideuid.IsChecked == true ? 1 : 0;
                    int clearCache = 1;

                    string message = $"{advanced},{enableFov},{sli_fov.Value:F1},{hideUid},{clearCache}";
                    byte[] data = Encoding.UTF8.GetBytes(message);
                    pipeClient.Write(data, 0, data.Length);

                    System.Threading.Thread.Sleep(100);

                    clearCache = 0;
                    message = $"{advanced},{enableFov},{sli_fov.Value:F1},{hideUid},{clearCache}";
                    data = Encoding.UTF8.GetBytes(message);
                    pipeClient.Write(data, 0, data.Length);

                    //MessageBox.Show("UID cache cleared successfully!\nThe system will re-search for UID widgets.", "Clear Cache", MessageBoxButton.OK, MessageBoxImage.Information);
                }
                else
                {
                    MessageBox.Show("Cannot clear cache: Game is not running.", "Clear Cache Error", MessageBoxButton.OK, MessageBoxImage.Warning);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to clear cache: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        //private Setup1 Setup1Window;
        private void Setup_Click(object sender, RoutedEventArgs e)
        {
            string exeparam = "";
            int exelauselect = 0;
            try
            {
                var parser = new FileIniDataParser();
                var data = parser.ReadFile(ConfigFileName);
                exeparam = data[Section][OptionKey6];
                //
                string exelauselectA = data[Section][OptionKey7];
                exelauselect = int.Parse(exelauselectA);
            }
            catch
            {

            }


            Setup1 windowab = new Setup1();

            // 设置模态窗口的Owner属性为主窗口
            windowab.Owner = this;
            // 保存主窗口的正常透明度
            double normal = this.Opacity;

            var parentTop = this.Top;
            var parentLeft = this.Left;
            var parentWidth = this.ActualWidth;
            var parentHeight = this.ActualHeight;
            double top = parentTop + (parentHeight - windowab.Height) / 2;
            double left = parentLeft + (parentWidth - windowab.Width) / 2;
            windowab.Top = top;
            windowab.Left = left;

            // 降低主窗口的透明度
            this.Opacity = 0.4;

            windowab.UpdateTextParam(exeparam);
            windowab.UpdateGameSelect(exelauselect);
            windowab.ShowDialog();

            // 模态窗口关闭后，恢复主窗口的正常透明度
            this.Opacity = normal;
        }


        private async void Start_Game(object sender, RoutedEventArgs e)
        {
            string exeparam1 = "";
            bool exeparam1open = true;
            int exechose = 1517;
            IniData data = null; // 在try块外部声明变量
            try
            {
                var parser = new FileIniDataParser();
                //var data = parser.ReadFile(ConfigFileName);
                data = parser.ReadFile(ConfigFileName);

                exeparam1 = data[Section][OptionKey6];
                if (exeparam1 == "")
                {
                    Console.WriteLine("param is empty");
                    exeparam1open = false;
                }

                string GameLaunchExeB = data[Section][OptionKey7];
                exechose = int.Parse(GameLaunchExeB);
            }
            catch
            {
                MessageBox.Show("INI file not exist", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }
            string GameServerA = data[Section][OptionKey5];
            GameServerAvalue = int.Parse(GameServerA);
            Console.WriteLine(GameServerAvalue + "GameServerAvalue");

            bool startgame_test = true;
            try
            {
                try
                {
                    int value = int.Parse(Tb_main.Text);
                    sli_main.Value = value;
                }
                catch (Exception)
                {
                    Tb_main.Text = sli_main.Value.ToString();
                }

                if (check_dx11.IsChecked == true)
                {
                    KillProcesses("Wuthering Waves");
                    KillProcesses("Client-Win64-Shipping");
                    KillProcesses("nvngx_update");
                    kill_ProcessManager_window.KillProcessByWindowName("鸣潮");
                    Process process = new Process();
                    var parser = new FileIniDataParser();
                    //var data = parser.ReadFile(ConfigFileName);
                    data = parser.ReadFile(ConfigFileName);
                    GlobalVariables.Path_EXE_launch = data[Section][InputKey2];
                    //process.StartInfo.FileName = GlobalVariables.Path_EXE_launch;

                    string targetExe = GlobalVariables.Path_EXE_launch;


                    if (exechose == 1)
                    {
                        // 获取不包含文件名的目录路径
                        string directoryPath = Path.GetDirectoryName(GlobalVariables.Path_EXE_launch);

                        // 定义新的文件路径
                        string newFilePath = "Client\\Binaries\\Win64\\Client-Win64-Shipping.exe";

                        //// 使用System.IO.Path.Combine来安全地组合路径
                        //process.StartInfo.FileName = Path.Combine(directoryPath, newFilePath);

                        targetExe = Path.Combine(directoryPath, newFilePath);
                    }


                    string args = "";
                    if (exeparam1open == false)
                    {
                        //process.StartInfo.Arguments = "-dx11";
                        args = "-dx11";
                    }
                    else
                    {
                        Console.WriteLine(exeparam1 + "启动参数1");
                        args = "-dx11 " + exeparam1;
                        //process.StartInfo.Arguments = "-dx11 " + exeparam1;
                    }


                    //try
                    //{
                    //    process.Start();
                    //}
                    //catch (Exception)
                    //{
                    //    startgame_test = false;
                    //}


                    string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                    string dllToInject = Path.Combine(baseDir, @"ulk_ww_tools\ww_ulk_plugin.dll");
                    string dllToInject_fps = Path.Combine(baseDir, @"ulk_ww_tools\ww_ulk_plugin_base.dll");

                    try
                    {
                        // 检查DLL是否存在
                        if (!File.Exists(dllToInject))
                        {
                            Console.WriteLine("注入DLL不存在: " + dllToInject);
                            startgame_test = false;
                            return;
                        }

                        // 检查EXE是否存在
                        if (!File.Exists(targetExe))
                        {
                            Console.WriteLine("启动程序不存在: " + targetExe);
                            startgame_test = false;
                            //return;
                        }

                        // 注入fpsDLL 26011
                        List<string> dlls = null;
                        dlls = new List<string> { dllToInject_fps };

                        //修复：在UI线程上获取cb_advanced的状态 260611
                        bool shouldinject = false;
                        Application.Current.Dispatcher.Invoke(() =>
                        {
                            shouldinject = cb_advanced.IsChecked == true;
                        });
                        if (shouldinject)
                        {
                            dlls.Add(dllToInject);
                        }

                        bool injectSuccess = StartAndInject(targetExe, args, dlls, true);

                        if (!injectSuccess)
                        {
                            startgame_test = false;
                            Console.WriteLine("启动或注入失败！");
                        }
                        else
                        {
                            Console.WriteLine("启动并注入成功！");
                        }
                    }
                    catch (Exception ex)
                    {
                        startgame_test = false;
                        Console.WriteLine("发生错误: " + ex.Message);
                    }


                    if (startgame_test == true)
                    {
                        bool _exechose = false;
                        if (exechose == 1)
                        {
                            _exechose = true;
                        }

                        Task.Run(() => Unlockstart_test(_exechose));
                        start_but.IsEnabled = false;
                        start_but.Content = "Running";
                        await Task.Delay(1531);
                        this.WindowState = WindowState.Minimized;
                    }
                    else
                    {
                        MessageBox.Show("Game EXE file not exist", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                        startgame_test1 = false;

                        if (check_auto_start.IsChecked == false)
                        {
                            checkautostart = true;
                        }

                        if (checkautostart == true)
                        {
                            Setup_Click(this, new RoutedEventArgs());
                        }
                    }

                }
                else
                {
                    KillProcesses("Wuthering Waves");
                    KillProcesses("Client-Win64-Shipping");
                    KillProcesses("nvngx_update");
                    kill_ProcessManager_window.KillProcessByWindowName("鸣潮");
                    Process process_non_dx11 = new Process();
                    var parser = new FileIniDataParser();
                    data = parser.ReadFile(ConfigFileName);
                    //var data = parser.ReadFile(ConfigFileName);
                    GlobalVariables.Path_EXE_launch = data[Section][InputKey2];
                    //process_non_dx11.StartInfo.FileName = GlobalVariables.Path_EXE_launch;

                    string targetExe = GlobalVariables.Path_EXE_launch;


                    if (exechose == 1)
                    {
                        // 获取不包含文件名的目录路径
                        string directoryPath = Path.GetDirectoryName(GlobalVariables.Path_EXE_launch);

                        // 定义新的文件路径
                        string newFilePath = "Client\\Binaries\\Win64\\Client-Win64-Shipping.exe";

                        // 使用System.IO.Path.Combine来安全地组合路径
                        //process_non_dx11.StartInfo.FileName = Path.Combine(directoryPath, newFilePath);

                        targetExe = Path.Combine(directoryPath, newFilePath);
                    }


                    string args = "";
                    if (exeparam1open == false)
                    {

                    }
                    else
                    {
                        Console.WriteLine(exeparam1 + "启动参数1");
                        //process_non_dx11.StartInfo.Arguments = exeparam1;
                        args = exeparam1;
                    }

                    //try
                    //{
                    //    process_non_dx11.Start();
                    //}
                    //catch (Exception)
                    //{
                    //    startgame_test = false;
                    //}


                    string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                    string dllToInject = Path.Combine(baseDir, @"ulk_ww_tools\ww_ulk_plugin.dll");
                    string dllToInject_fps = Path.Combine(baseDir, @"ulk_ww_tools\ww_ulk_plugin_base.dll");

                    try
                    {
                        // 检查DLL是否存在
                        if (!File.Exists(dllToInject))
                        {
                            Console.WriteLine("注入DLL不存在: " + dllToInject);
                            startgame_test = false;
                            return;
                        }

                        // 检查EXE是否存在
                        if (!File.Exists(targetExe))
                        {
                            Console.WriteLine("启动程序不存在: " + targetExe);
                            startgame_test = false;
                            //return;
                        }


                        // 注入fpsDLL 26011
                        List<string> dlls = null;
                        dlls = new List<string> { dllToInject_fps };

                        //修复：在UI线程上获取cb_advanced的状态 260611
                        bool shouldinject = false;
                        Application.Current.Dispatcher.Invoke(() =>
                        {
                            shouldinject = cb_advanced.IsChecked == true;
                        });
                        if (shouldinject)
                        {
                            dlls.Add(dllToInject);
                        }

                        bool injectSuccess = StartAndInject(targetExe, args, dlls, true);

                        if (!injectSuccess)
                        {
                            startgame_test = false;
                            Console.WriteLine("启动或注入失败！");
                        }
                        else
                        {
                            Console.WriteLine("启动并注入成功！");
                        }
                    }
                    catch (Exception ex)
                    {
                        startgame_test = false;
                        Console.WriteLine("发生错误: " + ex.Message);
                    }


                    if (startgame_test == true)
                    {
                        bool _exechose = false;
                        if (exechose == 1)
                        {
                            _exechose = true;
                        }

                        Task.Run(() => Unlockstart_test(_exechose));
                        start_but.IsEnabled = false;
                        start_but.Content = "Running";
                        await Task.Delay(1131);
                        this.WindowState = WindowState.Minimized;
                    }
                    else
                    {
                        MessageBox.Show("Game EXE file not exist", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                        startgame_test1 = false;
                        if (check_auto_start.IsChecked == false)
                        {
                            checkautostart = true;
                        }
                        if (checkautostart == true)
                        {
                            Setup_Click(this, new RoutedEventArgs());
                        }
                    }
                }

            }
            catch (FormatException)
            {
                MessageBox.Show("Launch Game Error!", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            catch (OverflowException)
            {

            }
        }


        public class kill_ProcessManager_window
        {
            [DllImport("user32.dll", SetLastError = true)]
            private static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

            [DllImport("user32.dll", SetLastError = true)]
            private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

            public static void KillProcessByWindowName(string windowName)
            {
                IntPtr hWnd = FindWindow(null, windowName);
                if (hWnd == IntPtr.Zero)
                {
                    Console.WriteLine($"window '{windowName}' not found");
                    return;
                }

                uint processId;
                GetWindowThreadProcessId(hWnd, out processId);

                try
                {
                    Process process = Process.GetProcessById((int)processId);
                    process.Kill();
                    Console.WriteLine($"process '{process.ProcessName}' stopped。");
                }
                catch (ArgumentException)
                {
                    Console.WriteLine($"can not find process ID  '{processId}' process");
                }
                catch (InvalidOperationException)
                {
                    Console.WriteLine($"can stop process ID '{processId}' it might be exit");
                }
                catch (SystemException ex)
                {
                    Console.WriteLine($"eorror when stop process {ex.Message}");
                }
            }
        }


        private void KillProcesses(string processName)
        {
            try
            {
                Process[] processes = Process.GetProcessesByName(processName);

                foreach (var process in processes)
                {
                    process.Kill();
                    process.WaitForExit();
                }

                Console.WriteLine($"{processes.Length} A {processName}.exe stopped");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"error hook: {ex.Message}");
            }
        }





        public void Unlockstart_test(bool gameexeselect)
        {
            bool gametest = true;
            int fail_times = 0;

            while (gametest)
            {
                try
                {
                    string processNametest = "Client-Win64-Shipping";

                    Process[] processes = Process.GetProcessesByName(processNametest);

                    if (processes.Length > 0)
                    {
                        Process process = processes[0];

                        Console.WriteLine($"{processNametest} is running");

                        if (gameexeselect)
                        {
                            Console.WriteLine("try to hook ClientWIN game with exe select");
                        }
                        else
                        {
                            Console.WriteLine("try to hook Wuthering Waves game without exe select");

                            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                            string dllToInject = Path.Combine(baseDir, @"ulk_ww_tools\ww_ulk_plugin.dll");
                            string dllToInject_fps = Path.Combine(baseDir, @"ulk_ww_tools\ww_ulk_plugin_base.dll");

                            List<string> dlls = new List<string>
                            {
                                dllToInject_fps
                            };

                            // 修复：在UI线程上获取cb_advanced状态
                            bool shouldinject = false;

                            Application.Current.Dispatcher.Invoke(() =>
                            {
                                shouldinject = cb_advanced.IsChecked == true;
                            });

                            if (shouldinject)
                            {
                                dlls.Add(dllToInject);
                            }

                            InjectOnly(processNametest, dlls);
                        }

                        break;
                    }
                    else
                    {
                        Console.WriteLine($"{processNametest} NOT launch");
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine("ERROR at Unlockstart test:");
                    Console.WriteLine(ex.ToString());

                    // MessageBox.Show(
                    //     "Injection failed.",
                    //     "Notification",
                    //     MessageBoxButton.OK,
                    //     MessageBoxImage.Warning);
                }

                Thread.Sleep(1);
                Console.WriteLine("try to hook ww game");

                // 去除弹窗提示 260611
                // Console.WriteLine(fail_times);
                // if (fail_times < 35)
                // {
                //     fail_times++;
                // }
                // if (fail_times == 30)
                // {
                //     Task.Run(() =>
                //     {
                //         Application.Current.Dispatcher.Invoke(() =>
                //         {
                //             MessageBox.Show(
                //                 "Could not unlock the FPS for more than 30s." + Environment.NewLine +
                //                 "The reason might be:" + Environment.NewLine +
                //                 "1. Wuthering Waves was not closed correctly" + Environment.NewLine +
                //                 "2. The user selected the wrong Wuthering Waves EXE file" + Environment.NewLine +
                //                 "3. Game version update causes memory address changes that cannot be modified",
                //                 "Notification",
                //                 MessageBoxButton.OK,
                //                 MessageBoxImage.Warning);

                //             this.Show();
                //             this.WindowState = WindowState.Normal;
                //         });
                //     });
                // }
            }

            Console.WriteLine("hook successfully");

            //pipeClient = null;
            //pipeClientFPS = null;

            Task.Run(() => Unlockstart());
        }




        private void werCK()
        {
            Console.WriteLine("Current " + ck_priority);

            // 设置进程名称
            string processNamer = "Client-Win64-Shipping";

            // 获取所有名为"Client-Win64-Shipping.exe"的进程
            Process[] processes = Process.GetProcessesByName(processNamer);

            foreach (Process processx in processes)
            {
                try
                {

                    // 设置进程优先级
                    // 可以选择以下优先级之一：
                    // ProcessPriorityClass.Idle, ProcessPriorityClass.BelowNormal,
                    // ProcessPriorityClass.Normal, ProcessPriorityClass.AboveNormal,
                    // ProcessPriorityClass.High, ProcessPriorityClass.RealTime
                    Console.WriteLine("进程优先级已设置为");
                    if (ck_priority == 6)
                    {
                        processx.PriorityClass = ProcessPriorityClass.RealTime;
                    }
                    if (ck_priority == 5)
                    {
                        processx.PriorityClass = ProcessPriorityClass.High;
                    }
                    if (ck_priority == 4)
                    {
                        processx.PriorityClass = ProcessPriorityClass.AboveNormal;
                    }
                    if (ck_priority == 3)
                    {
                        processx.PriorityClass = ProcessPriorityClass.Normal;
                    }
                    if (ck_priority == 2)
                    {
                        processx.PriorityClass = ProcessPriorityClass.BelowNormal;
                    }
                    if (ck_priority == 1)
                    {
                        processx.PriorityClass = ProcessPriorityClass.Idle;
                    }
                    if (ck_priority == 0)
                    {
                        Console.WriteLine("default priority");
                    }
                }
                catch (Exception e)
                {
                    // 输出错误信息
                    Console.WriteLine("无法设置进程优先级: " + e.Message);
                }
            }

            // 如果没有找到进程，则输出提示信息
            if (processes.Length == 0)
            {
                Console.WriteLine("没有找到名为 \"" + processNamer + "\" 的进程.");
            }
        }



        [StructLayout(LayoutKind.Sequential)]
        public struct MEMORY_BASIC_INFORMATION
        {
            public IntPtr BaseAddress;
            public IntPtr AllocationBase;
            public uint AllocationProtect;
            public IntPtr RegionSize;
            public uint State;
            public uint Protect;
            public uint Type;
        }

        [DllImport("kernel32.dll")]
        static extern int VirtualQueryEx(IntPtr hProcess, IntPtr lpAddress, out MEMORY_BASIC_INFORMATION lpBuffer, uint dwLength);

        [DllImport("kernel32.dll")]
        static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, [Out] byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);

        const int BLOCKMAXSIZE = 409600;
        private short[] Next = new short[260];
        private byte[] MemoryData = new byte[BLOCKMAXSIZE];

        private void GetNext(byte[] pattern)
        {
            for (int i = 0; i < Next.Length; i++)
            {
                Next[i] = -1;
            }
            for (int i = 0; i < pattern.Length; i++)
            {
                Next[pattern[i]] = (short)i;
            }
        }

        private void SearchMemoryBlock(IntPtr hProcess, byte[] pattern, ulong startAddress, int size, List<ulong> resultList)
        {
            byte[] buffer = new byte[size];
            IntPtr bytesRead;

            if (!ReadProcessMemory(hProcess, (IntPtr)startAddress, buffer, size, out bytesRead))
                return;

            for (int i = 0; i < buffer.Length;)
            {
                int j = i, k = 0;
                for (; k < pattern.Length && j < buffer.Length; k++, j++)
                {
                    if (pattern[k] != 0x100 && buffer[j] != pattern[k])
                        break;
                }

                if (k == pattern.Length)
                {
                    resultList.Add(startAddress + (ulong)i);
                }

                if (i + pattern.Length >= buffer.Length)
                    return;

                short shift = Next[buffer[i + pattern.Length]];
                if (shift == -1)
                    i += pattern.Length - Next[0x100];
                else
                    i += pattern.Length - shift;
            }
        }

        private int SearchMemory(IntPtr hProcess, string pattern, ulong startAddress, ulong endAddress, int initSize, List<ulong> resultList)
        {
            List<byte> tzmList = new List<byte>();
            string[] bytes = pattern.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);

            foreach (string b in bytes)
            {
                if (b == "?")
                {
                    // 通配符标记，这里可以添加一个特定的byte值来表示通配符，例如 0xFF
                    // 确保这个值不会与实际的数据冲突
                    tzmList.Add(0xFF);
                }
                else
                {
                    tzmList.Add(Convert.ToByte(b, 16));
                }
            }

            byte[] tzmArray = tzmList.ToArray();
            GetNext(tzmArray);

            MEMORY_BASIC_INFORMATION mbi = new MEMORY_BASIC_INFORMATION();
            uint mbiSize = (uint)Marshal.SizeOf(typeof(MEMORY_BASIC_INFORMATION));

            while (VirtualQueryEx(hProcess, (IntPtr)startAddress, out mbi, mbiSize) != 0)
            {
                if (mbi.Protect == 0x04 || mbi.Protect == 0x40) // PAGE_READWRITE || PAGE_EXECUTE_READWRITE
                {
                    ulong regionSize = (ulong)mbi.RegionSize.ToInt64();
                    ulong currentAddress = startAddress;

                    while (regionSize > 0)
                    {
                        int blockSize = (int)Math.Min(regionSize, (ulong)BLOCKMAXSIZE);
                        SearchMemoryBlock(hProcess, tzmArray, currentAddress, blockSize, resultList);
                        currentAddress += (ulong)blockSize;
                        regionSize -= (ulong)blockSize;
                    }
                }

                startAddress += (ulong)mbi.RegionSize.ToInt64();
                if (endAddress != 0 && startAddress > endAddress) break;
            }

            return resultList.Count;
        }



        public void Unlockstart()
        {

            //static bool IsProcessRunningWithWindow(string processNamea)
            //{
            //    //Process[] processes = Process.GetProcessesByName(processNamea);
            //    //foreach (var process in processes)
            //    //{
            //    //    //if (process.MainWindowHandle != IntPtr.Zero)
            //    //    if (!process.HasExited && process.MainWindowHandle != IntPtr.Zero)
            //    //    {
            //    //        return true;
            //    //    }
            //    //}
            //    //return false;

            //    bool isRunning = Utils.Natives.IsUnrealWindowRunning(processNamea);

            //    if (isRunning)
            //    {
            //        Console.WriteLine("检测到目标进程，并且 UnreaWindow 窗口已经存在！");
            //        //Thread.Sleep(10117); // 260610等待3秒钟
            //        return true;
            //    }
            //    else
            //    {
            //        Console.WriteLine("目标进程未运行，或者 UnrealWindow 尚未加载出来。");
            //        return false;
            //    }
            //}

            //bool checkwwrun = true;
            //while (checkwwrun)
            //{
            //    string processNameb = "Client-Win64-Shipping";

            //    if (IsProcessRunningWithWindow(processNameb))
            //    {
            //        Console.WriteLine($"{processNameb} is running with main window");

            //        string processNametest = "Client-Win64-Shipping";
            //        Process[] processes = Process.GetProcessesByName(processNametest);
            //        //Console.WriteLine(GameServerAvalue + "A123456");
            //        if (processes.Length > 0 && GameServerAvalue == 2)
            //        {
            //            try
            //            {
            //                Process process = processes[0];

            //                // 设置超时时间，防止死循环
            //                int timeout = 5000; // 5秒
            //                int elapsedTime = 0;

            //                // 循环直到 MainModule 不为 null 或超时
            //                while (process.MainModule.FileName == null && elapsedTime < timeout)
            //                {
            //                    Thread.Sleep(100); // 每隔100毫秒检查一次
            //                    elapsedTime += 100;
            //                }

            //                // 检查是否超时
            //                if (elapsedTime >= timeout)
            //                {
            //                    Console.WriteLine("Timeout: process.MainModule is still null.");
            //                }
            //                else
            //                {
            //                    // process.MainModule 已经不为 null
            //                    string procClientWin64Directory = Path.GetDirectoryName(process.MainModule.FileName);
            //                    string targetDirectoryGL = Path.Combine(procClientWin64Directory, "ThirdParty\\KrPcSdk_Global");
            //                    string targetDirectoryCN = Path.Combine(procClientWin64Directory, "ThirdParty\\KrPcSdk_Mainland");

            //                    if (Directory.Exists(targetDirectoryCN))
            //                    {
            //                        Console.WriteLine($"GAME VERSION IS CN {targetDirectoryCN} exists.");
            //                        GameServerAresult = 1;
            //                    }
            //                    if (Directory.Exists(targetDirectoryGL))
            //                    {
            //                        Console.WriteLine($"GAME VERSION IS GLOBAL {targetDirectoryGL} exists.");
            //                        GameServerAresult = 2;
            //                    }
            //                }
            //            }
            //            catch (Exception ex)
            //            {
            //                Console.WriteLine("TEST GAME AREA ERROR " + ex.Message);
            //            }
            //        }
            //        else
            //        {
            //            Console.WriteLine($"area detect closed {processNametest}");
            //        }



            //        break;
            //    }
            //    else
            //    {
            //        Console.WriteLine($"{processNameb} is NOT running with main window。");
            //    }
            //    Thread.Sleep(315);
            //}


            try
            {
                //const int PROCESS_QUERY_INFORMATION = 0x0400;
                //const int PROCESS_VM_READ = 0x0010;
                //const int PROCESS_VM_WRITE = 0x0020;
                //const int PROCESS_VM_OPERATION = 0x0008;
                const int PROCESS_ALL_ACCESS = 0x1F0FFF;

                [DllImport("kernel32.dll")]
                static extern IntPtr OpenProcess(int dwDesiredAccess, bool bInheritHandle, int dwProcessId);

                //[DllImport("kernel32.dll")]
                //static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);

                [DllImport("kernel32.dll")]
                static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesWritten);

                [DllImport("user32.dll")]
                static extern IntPtr GetForegroundWindow();

                [DllImport("user32.dll")]
                static extern int GetWindowThreadProcessId(IntPtr hWnd, out int lpdwProcessId);





                string processName = "Client-Win64-Shipping";
                Process process = Process.GetProcessesByName(processName)[0];

                if (process == null)
                {
                    Console.WriteLine("Process not found.");
                    return;
                }

                //IntPtr processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, false, process.Id);

                goto SkipProcess_260612;

                //250301
                int pid;
                pid = process.Id;
                IntPtr hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
                if (hProcess == IntPtr.Zero)
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }

                List<ulong> resultList = new List<ulong>();
                string pattern = "67 00 63 00 2E 00 4C 00 6F 00 77 00 4D 00 65 00 6D 00 6F 00 72 00 79 00 2E 00 49 00 6E 00 63 00 72 00 65 00 6D 00 65 00 6E 00 74 00 61 00 6C 00 47 00 43 00 54 00 69 00 6D 00 65 00 50 00 65 00 72 00 46 00 72 00 61 00 6D 00 65 00 00 00 00 00 4E 00 75 00 6D 00 20 00 74 00 68 00 72 00 65 00 73 00 68 00 6F 00 6C 00 64 00 20 00 66 00 6F 00 72 00 20 00 6C 00 6F 00 77 00 20 00 6F 00 62 00 6A 00 65 00 63 00 74 00 73 00 20 00 47 00 43 00 20 00 6D 00 6F 00 64 00 65 00";






                //string targetModuleName = "Client-Win64-Shipping.exe";

                //// 获取模块信息
                //MODULEINFO targetModuleInfo = new MODULEINFO();
                //bool found = false;
                //uint bytesNeeded;
                //if (EnumProcessModules(hProcess, null, 0, out bytesNeeded))
                //{
                //    IntPtr[] modules = new IntPtr[bytesNeeded / IntPtr.Size];
                //    if (EnumProcessModules(hProcess, modules, bytesNeeded, out bytesNeeded))
                //    {
                //        foreach (IntPtr module in modules)
                //        {
                //            StringBuilder moduleName = new StringBuilder(260);
                //            if (GetModuleFileNameEx(hProcess, module, moduleName, (uint)moduleName.Capacity) != 0)
                //            {
                //                string name = Path.GetFileName(moduleName.ToString());
                //                if (name.Equals(targetModuleName, StringComparison.OrdinalIgnoreCase))
                //                {
                //                    if (GetModuleInformation(hProcess, module, out targetModuleInfo, (uint)Marshal.SizeOf(typeof(MODULEINFO))))
                //                    {
                //                        found = true;
                //                        break;
                //                    }
                //                }
                //            }
                //        }
                //    }
                //}

                //if (!found)
                //{
                //    // 处理模块未找到的情况
                //    Console.WriteLine("目标模块未找到");
                //    return;
                //}

                //ulong baseAddress = (ulong)targetModuleInfo.lpBaseOfDll.ToInt64();
                //ulong endAddress = baseAddress + targetModuleInfo.SizeOfImage;

                //// 调用搜索函数
                //var watch = Stopwatch.StartNew();
                //SearchMemory(hProcess, pattern, baseAddress, endAddress, 30, resultList);
                //watch.Stop();









                var watch = Stopwatch.StartNew();
                SearchMemory(hProcess, pattern, 0x7f, 0x7fffffffffff, 30, resultList);
                watch.Stop();

                Console.WriteLine($"用时：{watch.ElapsedMilliseconds} 毫秒");
                Console.WriteLine($"搜索到 {resultList.Count} 个结果");

                if (resultList.Count == 0)
                {
                    Task.Run(() =>
                    {
                        Application.Current.Dispatcher.Invoke(() =>
                        {
                            MessageBox.Show("Could not find fps pattern" + Environment.NewLine +
                "The reason might be:" + Environment.NewLine +
                "1. Wuthering Waves was not closed correctly" + Environment.NewLine +
                "2. The user selected the wrong Wuthering Waves EXE file" + Environment.NewLine +
                "3. Game version update causes memory address changes that cannot be modified",
                "Notification",
                MessageBoxButton.OK,
                MessageBoxImage.Warning);
                            isunlockFPSErrorMSG = true;
                            try
                            {
                                // Show the window
                                this.Show();
                                this.WindowState = WindowState.Normal;
                            }
                            catch
                            {
                            }
                        });
                    });

                    Task.Run(() =>
                    {
                        Console.WriteLine("Could not unlock the FPS");
                        while (true)
                        {
                            if (isShutdownCompleted == true)
                            {
                                Environment.Exit(0);
                            }
                            if (isunlockFPSErrorMSG == true)
                            {
                                break;
                            }

                            Thread.Sleep(500);
                        }
                    });
                }

                foreach (ulong addr1 in resultList)
                {
                    Console.WriteLine($"0x{addr1:X}");
                }




                //SELECT CN OR GLOBAL
                IntPtr moduleBaseAddress = IntPtr.Zero;
                if (GameServerAvalue == 0 || GameServerAresult == 1)
                {
                    //CN server
                    moduleBaseAddress = process.MainModule.BaseAddress + 0x08618588;
                }
                if (GameServerAvalue == 1 || GameServerAresult == 2)
                {
                    //GLOBAL server
                    moduleBaseAddress = process.MainModule.BaseAddress + 0x085335E8;
                }
                if (GameServerAvalue == 2 && GameServerAresult == -1)
                {
                    Console.WriteLine("不能找到地区");
                    //MessageBox.Show("Auto detect game server error,Please manually select the game server", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    //goto detectgameservererror;
                }

                //IntPtr moduleBaseAddress = process.MainModule.BaseAddress + 0x08618588;
                //IntPtr moduleBaseAddress = process.MainModule.BaseAddress + 0x0822C530;

                //int rootAddress;
                //byte[] rootBuffer = new byte[4];
                //ReadProcessMemory(processHandle, moduleBaseAddress, rootBuffer, rootBuffer.Length, out IntPtr bytesRead);
                //rootAddress = BitConverter.ToInt32(rootBuffer, 0);

                //float tmpValue = 0;
                //byte[] tmpBuffer = new byte[4];


                //ReadProcessMemory(processHandle, (IntPtr)(rootAddress + 0x48), tmpBuffer, tmpBuffer.Length, out IntPtr bytesReadTmp);
                //tmpValue = BitConverter.ToSingle(tmpBuffer, 0);



                int checkcount = 0;
                bool checkaddress = true;
                //250301
                checkaddress = false;
                while (checkaddress)
                {
                    //if (tmpValue == 30)
                    //{
                    //    break;
                    //}
                    //else
                    //{
                    //    if (tmpValue == 45)
                    //    {
                    //        break;
                    //    }
                    //    else
                    //    {
                    //        if (tmpValue == 60)
                    //        {
                    //            break;
                    //        }
                    //        else
                    //        {
                    //            if (tmpValue == 120)
                    //            {
                    //                break;
                    //            }
                    //        }
                    //    }
                    //}


                    //    ReadProcessMemory(processHandle, (IntPtr)(rootAddress + 0x0), tmpBuffer, tmpBuffer.Length, out IntPtr bytesReadTmp);
                    //    tmpValue = BitConverter.ToSingle(tmpBuffer, 0);
                    //    //if (tmpValue >= 30f && tmpValue <= 120f)
                    //    //{
                    //    //    break;
                    //    //}
                    //    if (tmpValue == 30 || tmpValue == 45 || tmpValue == 60 || tmpValue == 120)
                    //    {
                    //        break;
                    //    }
                    //    else
                    //    {
                    //        if (checkcount <= 31)
                    //        {
                    //            checkcount++;

                    //            if (process.HasExited)
                    //            {
                    //                Thread.Sleep(315);
                    //                Application.Current.Dispatcher.Invoke(() =>
                    //                {
                    //                    Application.Current.Shutdown();
                    //                });
                    //            }
                    //        }
                    //    }
                    //    if (checkcount == 21)
                    //    {
                    //        //MessageBox.Show("Failed to hook FPS" + Environment.NewLine + "寄" + Environment.NewLine + "寄","Error",MessageBoxButtons.OK,MessageBoxIcon.Error);




                    //        //// 在新线程中显示消息框
                    //        //Thread messageBoxThread = new Thread(() =>
                    //        //{
                    //        //    MessageBox.Show("Failed to hook FPS" + Environment.NewLine + "寄" + Environment.NewLine + "寄", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    //        //});
                    //        //messageBoxThread.IsBackground = true; // 设置为后台线程，这样它不会阻止程序退出
                    //        //messageBoxThread.Start();

                    //        Task.Run(() =>
                    //        {
                    //            Application.Current.Dispatcher.Invoke(() =>
                    //            {
                    //                MessageBox.Show("Could not unlock the FPS" + Environment.NewLine +
                    //    "The reason might be:" + Environment.NewLine +
                    //    "1. Wuthering Waves was not closed correctly" + Environment.NewLine +
                    //    "2. The user selected the wrong Wuthering Waves EXE file" + Environment.NewLine +
                    //    "3. Game version update causes memory address changes that cannot be modified",
                    //    "Notification",
                    //    MessageBoxButton.OK,
                    //    MessageBoxImage.Warning);
                    //                isunlockFPSErrorMSG = true;
                    //                try
                    //                {
                    //                    // Show the window
                    //                    this.Show();
                    //                    this.WindowState = WindowState.Normal;
                    //                }
                    //                catch 
                    //                { 
                    //                }
                    //            });
                    //        });

                    //        Task.Run(() =>
                    //        {
                    //            Console.WriteLine("Could not unlock the FPS");
                    //            while (true)
                    //            {
                    //                if (isShutdownCompleted == true)
                    //                {
                    //                    Environment.Exit(0);
                    //                }
                    //                if (isunlockFPSErrorMSG == true)
                    //                {
                    //                    break;
                    //                }

                    //                Thread.Sleep(500);
                    //            }
                    //        });
                    //        break;
                    //    }
                    //    Console.WriteLine("fps " + tmpValue);
                    //    Console.WriteLine("TIME " + checkcount);
                    //    Thread.Sleep(1000);
                }


            SkipProcess_260612:


                HashSet<int> clientProcessIds = new HashSet<int>();

                // 在循环外获取 Client-Win64-Shipping 进程列表
                var clientProcesses = Process.GetProcessesByName("Client-Win64-Shipping");
                foreach (var process_sav in clientProcesses)
                {
                    clientProcessIds.Add(process_sav.Id);
                }


                // 设置进程名称
                string processNamer = "Client-Win64-Shipping";
                // 获取所有名为"Client-Win64-Shipping.exe"的进程
                Process[] processes = Process.GetProcessesByName(processNamer);


                foreach (Process process1 in processes)
                {
                    try
                    {
                        // 检查进程的优先级
                        ProcessPriorityClass priority1 = process1.PriorityClass;

                        // 打印进程的优先级
                        Console.WriteLine($"进程 \"{process1.ProcessName}\" (ID: {process1.Id}) 的优先级是: {priority1}.");

                        //// 用于存储获取到的优先级
                        //ProcessPriorityClass? priorityToSet = null;

                        //// 获取第一个进程的优先级
                        //priorityToSet = processes[0].PriorityClass;

                        //process1.PriorityClass = priorityToSet.Value;


                    }
                    catch (Exception e)
                    {
                        // 输出错误信息
                        Console.WriteLine("无法设置进程优先级: " + e.Message);
                    }
                }



                // 在循环开始前检查一次焦点
                IntPtr foregroundWindowHandle1 = GetForegroundWindow();
                int activeProcessId1;
                GetWindowThreadProcessId(foregroundWindowHandle1, out activeProcessId1);

                bool isInFocus1 = clientProcessIds.Contains(activeProcessId1);
                if (!isInFocus1)
                {
                    Console.WriteLine("Client-Win64-Shipping.exe is not in focus on startup.");
                    if (check_power_saving_sre == true)
                    {
                        foreach (Process processx in processes)
                        {
                            try
                            {
                                // 设置进程优先级
                                // 可以选择以下优先级之一：
                                // ProcessPriorityClass.Idle, ProcessPriorityClass.BelowNormal,
                                // ProcessPriorityClass.Normal, ProcessPriorityClass.AboveNormal,
                                // ProcessPriorityClass.High, ProcessPriorityClass.RealTime
                                processx.PriorityClass = ProcessPriorityClass.Idle;
                            }
                            catch (Exception e)
                            {
                                // 输出错误信息
                                Console.WriteLine("无法设置进程优先级: " + e.Message);
                            }
                        }

                        // 如果没有找到进程，则输出提示信息
                        if (processes.Length == 0)
                        {
                            Console.WriteLine("没有找到名为 \"" + processNamer + "\" 的进程.");
                        }
                    }
                }

                //250301

                //Task.Run(() =>
                //{
                //    while (true)
                //    {
                //        try
                //        {
                //            // 获取名为"Client-Win64-Shipping.exe"的所有进程实例
                //            Process[] processes12 = Process.GetProcessesByName("Client-Win64-Shipping");

                //            foreach (Process processdd in processes12)
                //            {
                //                // 获取主模块信息
                //                processdd.Refresh();

                //                ProcessModule mainModuledd = processdd.MainModule;
                //                if (mainModuledd != null)
                //                {
                //                    // 输出主模块的文件名和模块内存大小
                //                    Console.WriteLine("Process: {0}, Main Module: {1}, Module Memory Size: {2}",
                //                        processdd.Id, mainModuledd.ModuleName, mainModuledd.ModuleMemorySize);
                //                }
                //            }
                //        }
                //        catch (Exception ex)
                //        {
                //            // 输出异常信息
                //            Console.WriteLine("An error occurred: " + ex.Message);
                //        }

                //        // 等待一段时间后再次检查
                //        Thread.Sleep(1000); // 等待时间可以根据需要调整
                //    }

                //});


                goto SkipProcess_260612_main;

                //250301
                int findpatterntest = 0;
                Task.Run(() =>
                {
                    while (true)
                    {
                        //try
                        //{
                        //    ProcessModule mainModule = process.MainModule;
                        //    if (mainModule != null)
                        //    {
                        //        // 输出主模块的文件名和模块内存大小
                        //        Console.WriteLine("Process: {0}, Main Module: {1}, Module Memory Size: {2}",
                        //        process.Id, mainModule.ModuleName, mainModule.ModuleMemorySize);
                        //        checkcount = 0;
                        //    }
                        //}
                        //catch (Exception ex)
                        //{
                        //    // 输出异常信息
                        //    Console.WriteLine("An error occurred: " + ex.Message);
                        //}


                        Console.WriteLine("TIME " + checkcount);
                        Thread.Sleep(1000);
                        if (findpatterntest == 2)
                        {
                            break;
                        }
                        else
                        {
                            if (checkcount <= 915)
                            {
                                checkcount++;

                                if (process.HasExited)
                                {
                                    Thread.Sleep(315);
                                    Application.Current.Dispatcher.Invoke(() =>
                                    {
                                        Application.Current.Shutdown();
                                    });
                                }
                            }
                        }
                        if (checkcount == 911)
                        {
                            Task.Run(() =>
                            {
                                Application.Current.Dispatcher.Invoke(() =>
                                {
                                    MessageBox.Show("Could not unlock the FPS" + Environment.NewLine +
                        "The reason might be:" + Environment.NewLine +
                        "1. Wuthering Waves was not closed correctly" + Environment.NewLine +
                        "2. The user selected the wrong Wuthering Waves EXE file" + Environment.NewLine +
                        "3. Game version update causes memory address changes that cannot be modified",
                        "Notification",
                        MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                                    isunlockFPSErrorMSG = true;
                                    try
                                    {
                                        // Show the window
                                        this.Show();
                                        this.WindowState = WindowState.Normal;
                                    }
                                    catch
                                    {
                                    }
                                });
                            });

                            Task.Run(() =>
                            {
                                Console.WriteLine("Could not unlock the FPS");
                                while (true)
                                {
                                    if (isShutdownCompleted == true)
                                    {
                                        Environment.Exit(0);
                                    }
                                    if (isunlockFPSErrorMSG == true)
                                    {
                                        break;
                                    }

                                    Thread.Sleep(500);
                                }
                            });
                            break;
                        }
                    }
                });


                //250301
                byte[] buffer = new byte[1]; // 只需要1个字节的缓冲区来读取特征码
                IntPtr bytesRead;
                ulong foundAddress = 0;
                ulong addr;
                for (int i = 0; i < resultList.Count; i++)
                {
                    addr = resultList[i];
                    //Console.WriteLine("0x{0:X}", addr);
                    foundAddress = 0;

                    while (addr < 0x7fffffffffff) // 确保不会超出内存搜索范围
                    {
                        if (ReadProcessMemory(hProcess, (IntPtr)addr, buffer, 1, out bytesRead) && bytesRead.ToInt32() == 1)
                        {
                            if (buffer[0] == 0x70) // 检查是否找到特征码"70"
                            {
                                foundAddress = addr;
                                Console.WriteLine("找到特征码'70'的位置: 0x{0:X}", addr);
                                findpatterntest = findpatterntest + 1;
                                break; // 找到后立即停止搜索
                            }

                        }
                        else
                        {
                            //Console.WriteLine("12345678909776A");
                            Console.WriteLine("读取内存失败!");
                            break; // 读取失败时停止搜索
                        }
                        addr++; // 向0xfff方向搜索，每次增加1
                    }

                    if (foundAddress != 0)
                    {
                        // 这里可以添加修改内存的代码
                    }
                }


                // 用于存储找到的地址
                List<ulong> foundAddresses = new List<ulong>();

                if (foundAddress != 0)
                {
                    // 限定搜索范围
                    ulong rangeStart = addr; // 记录从当前位置开始的范围
                    ulong rangeEnd = rangeStart - 0x1000; // 限定最大搜索范围为1000字节

                    byte[] buffer1 = new byte[4];
                    float value;

                    //// 用于存储找到的地址
                    //List<ulong> foundAddresses = new List<ulong>();

                    // 来回搜索
                    while (addr > rangeEnd && addr > 0)
                    {
                        // 读取内存数据
                        if (ReadProcessMemory(hProcess, (IntPtr)addr, buffer1, 4, out bytesRead) && bytesRead.ToInt32() == 4)
                        {
                            value = BitConverter.ToSingle(buffer1, 0); // 将读取的字节转换为浮点数
                            if (value == 30.0f || value == 45.0f || value == 60.0f || value == 120.0f) // 检查是否为目标值
                            {
                                foundAddresses.Add(addr); // 将找到的地址添加到列表中
                                Console.WriteLine("找到值为{0}的单浮点数地址: 0x{1:X}", value, addr);

                                // 如果找到的地址数量已经是 2 个，跳出循环
                                if (foundAddresses.Count == 2)
                                {
                                    findpatterntest = findpatterntest + 1;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            //Console.WriteLine("12345678909776B");
                            Console.WriteLine("读取内存失败!");
                            break;
                            return; // 读取失败时停止搜索
                        }

                        // 每次减少1字节，单浮点数1字节
                        addr -= 1; // 每次步进1字节

                        // 如果超出了范围则从起始位置开始搜索
                        if (addr <= rangeEnd)
                        {
                            //Console.WriteLine("已到达范围底部，重新向上搜索...");
                            addr = rangeStart; // 重新设置起始地址，开始从头搜索
                            rangeEnd = rangeStart - 0x1000; // 重新设定搜索范围
                        }
                    }

                    // 如果找到了两个地址，打印第二个地址
                    if (foundAddresses.Count == 2)
                    {
                        Console.WriteLine("第二个找到的地址: 0x{0:X}", foundAddresses[1]);


                        //byte[] valueToWrite = BitConverter.GetBytes(215.0f); // 将70转换为字节
                        //IntPtr bytesWritten;

                        //bool testsuccunlock = false;
                        //// 循环写入值70
                        //while (true)
                        //{
                        //    if (WriteProcessMemory(hProcess, (IntPtr)foundAddresses[1], valueToWrite, 4, out bytesWritten) && bytesWritten.ToInt32() == 4)
                        //    {
                        //        if (!testsuccunlock)
                        //        {
                        //            Console.WriteLine("成功写入值70到地址: 0x{0:X}", foundAddresses[1]);
                        //            // 这里可以添加一些逻辑来决定何时停止循环，例如：
                        //            // if (conditionToStop) break;
                        //            testsuccunlock = true;
                        //        }
                        //    }
                        //    else
                        //    {
                        //        Console.WriteLine("写入内存失败!");
                        //        break; // 写入失败时停止循环
                        //    }
                        //    Thread.Sleep(115);
                        //}
                    }
                }





            SkipProcess_260612_main:

                //250301
                IntPtr bytesWritten;
                bool testsuccunlock = false;
                //
                bool tagck = false;
                bool wasInFocus = false;
                bool mcklIsRunning = true;
                int newck_priority2 = 0;
                while (mcklIsRunning)
                {
                    int newck_priority;
                    lock (_lock2)
                    {
                        newck_priority = ck_priority;
                    }

                    if (newck_priority2 != newck_priority)
                    {
                        if (newck_priority != 0)
                        {
                            if (check_power_saving_sre == false)
                            {
                                Console.WriteLine("different");
                                //if(newck_priority == 6 || newck_priority == 5 || newck_priority == 4 || newck_priority == 3 || newck_priority == 2 || newck_priority == 1)
                                //{ 
                                werCK();
                                //}
                            }
                        }
                    }
                    newck_priority2 = newck_priority;


                    bool check_power_saving_sre_off = false;
                    if (check_power_saving_sre == true)
                    {
                        //Console.WriteLine("check is ok");

                        tagck = true;
                        IntPtr foregroundWindowHandle = GetForegroundWindow();
                        int activeProcessId;
                        GetWindowThreadProcessId(foregroundWindowHandle, out activeProcessId);

                        bool isInFocus = clientProcessIds.Contains(activeProcessId);

                        if (isInFocus && !wasInFocus) // 只有当进入焦点且之前不在焦点时才输出
                        {
                            //Console.WriteLine("Client-Win64-Shipping.exe is in focus.");
                            wasInFocus = true; // 更新状态，表示已经输出过
                            foreach (Process processx in processes)
                            {
                                try
                                {
                                    if (check_power_saving_sre == true)
                                    {
                                        processx.PriorityClass = ProcessPriorityClass.Normal;
                                    }
                                }
                                catch (Exception e)
                                {
                                    // 输出错误信息
                                    Console.WriteLine("无法设置进程优先级: " + e.Message);
                                }
                            }
                            werCK();
                        }
                        else if (!isInFocus && wasInFocus) // 如果离开了焦点，更新状态
                        {
                            //Console.WriteLine("Client-Win64-Shipping.exe NOT in focus.");
                            wasInFocus = false;
                            Console.WriteLine("进程优先级已设置为省电");


                            //foreach (Process processx in processes)
                            //{
                            //    try
                            //    {
                            //        // 设置进程优先级
                            //        // 可以选择以下优先级之一：
                            //        // ProcessPriorityClass.Idle, ProcessPriorityClass.BelowNormal,
                            //        // ProcessPriorityClass.Normal, ProcessPriorityClass.AboveNormal,
                            //        // ProcessPriorityClass.High, ProcessPriorityClass.RealTime
                            //        if (check_power_saving_sre == true)
                            //        {
                            //            processx.PriorityClass = ProcessPriorityClass.Idle;
                            //        }
                            //    }
                            //    catch (Exception e)
                            //    {
                            //        // 输出错误信息
                            //        Console.WriteLine("无法设置进程优先级: " + e.Message);
                            //    }
                            //}

                            //// 如果没有找到进程，则输出提示信息
                            //if (processes.Length == 0)
                            //{
                            //    Console.WriteLine("没有找到名为 \"" + processNamer + "\" 的进程.");
                            //}
                        }

                        if (isInFocus)
                        {
                            //Console.WriteLine("Client-Win64-Shipping.exe is in focus.");
                        }
                        else
                        {
                            //Console.WriteLine("Client-Win64-Shipping.exe is not in focus.");
                            if (tagck2 == false)
                            {
                                Console.WriteLine("tagck2 set low Client-Win64-Shipping.exe");
                                tagck2 = true;
                                foreach (Process processx in processes)
                                {
                                    try
                                    {
                                        if (check_power_saving_sre == true)
                                        {
                                            processx.PriorityClass = ProcessPriorityClass.Idle;
                                        }
                                    }
                                    catch (Exception e)
                                    {
                                        // 输出错误信息
                                        Console.WriteLine("无法设置进程优先级: " + e.Message);
                                    }
                                }
                            }
                            check_power_saving_sre_off = true;
                        }
                    }

                    else
                    {
                        if (tagck == true)
                        {
                            foreach (Process processx in processes)
                            {
                                try
                                {
                                    tagck = false;
                                    Console.WriteLine("Client-Win64-Shipping.exe");
                                    processx.PriorityClass = ProcessPriorityClass.Normal;
                                    werCK();
                                }
                                catch (Exception e)
                                {
                                    // 输出错误信息
                                    Console.WriteLine("无法设置进程优先级: " + e.Message);
                                }
                            }
                        }
                    }


                    float newValue;
                    lock (_lock)
                    {
                        newValue = _newValue;
                    }

                    if (check_power_saving_sre_off == true)
                    {
                        if (newValue > 15 || newValue == 0)
                        {
                            newValue = 15.0f;
                        }
                    }

                    //byte[] newValueBuffer = BitConverter.GetBytes(newValue);


                    if (process.HasExited)
                    {
                        //Console.WriteLine($"{processName} is NOT running with main window。");
                        Thread.Sleep(315);
                        //Application.Current.Dispatcher.Invoke(() =>
                        //{
                        //    // 订阅应用程序的退出事件
                        //    Application.Current.Exit += (sender, e) =>
                        //    {
                        //        // 当应用程序退出时，将 isShutdownCompleted 设置为 true
                        //        isShutdownCompleted = true;
                        //    };

                        //    // 关闭应用程序
                        //    Application.Current.Shutdown();
                        //});
                        // 使用 Dispatcher 调度到 UI 线程
                        if (Application.Current != null)
                        {
                            Application.Current.Dispatcher.Invoke(() =>
                            {
                                // 订阅应用程序的退出事件
                                Application.Current.Exit += (sender, e) =>
                                {
                                    // 当应用程序退出时，将 isShutdownCompleted 设置为 true
                                    isShutdownCompleted = true;
                                };

                                // 关闭应用程序
                                Application.Current.Shutdown();
                            });
                        }
                        else
                        {
                            // 如果 Application.Current 是 null，输出调试信息
                            Console.WriteLine("Application.Current is null.");
                        }
                    }

                    //Console.WriteLine("123456789G");

                    // 注释写入代码改为DLL注入后由DLL写入，保持循环以持续更新值 260612
                    //byte[] valueToWrite = BitConverter.GetBytes(newValue);
                    //if (WriteProcessMemory(hProcess, (IntPtr)foundAddresses[1], valueToWrite, 4, out bytesWritten) && bytesWritten.ToInt32() == 4)
                    //{
                    //    if (!testsuccunlock)
                    //    {
                    //        Console.WriteLine("成功写入值70到地址: 0x{0:X}", foundAddresses[1]);
                    //        // 这里可以添加一些逻辑来决定何时停止循环，例如：
                    //        // if (conditionToStop) break;
                    //        testsuccunlock = true;
                    //    }
                    //}




                    //WriteProcessMemory(processHandle, (IntPtr)(rootAddress + 0x0), newValueBuffer, newValueBuffer.Length, out IntPtr bytesWritten);
                    Thread.Sleep(115);



                    //if (process.HasExited)
                    //{
                    //    //Console.WriteLine($"{processName} is NOT running with main window。");
                    //    Thread.Sleep(315);
                    //    //Application.Current.Dispatcher.Invoke(() =>
                    //    //{
                    //    //    // 订阅应用程序的退出事件
                    //    //    Application.Current.Exit += (sender, e) =>
                    //    //    {
                    //    //        // 当应用程序退出时，将 isShutdownCompleted 设置为 true
                    //    //        isShutdownCompleted = true;
                    //    //    };

                    //    //    // 关闭应用程序
                    //    //    Application.Current.Shutdown();
                    //    //});
                    //    // 使用 Dispatcher 调度到 UI 线程
                    //    if (Application.Current != null)
                    //    {
                    //        Application.Current.Dispatcher.Invoke(() =>
                    //        {
                    //            // 订阅应用程序的退出事件
                    //            Application.Current.Exit += (sender, e) =>
                    //            {
                    //                // 当应用程序退出时，将 isShutdownCompleted 设置为 true
                    //                isShutdownCompleted = true;
                    //            };

                    //            // 关闭应用程序
                    //            Application.Current.Shutdown();
                    //        });
                    //    }
                    //    else
                    //    {
                    //        // 如果 Application.Current 是 null，输出调试信息
                    //        Console.WriteLine("Application.Current is null.");
                    //    }
                    //}





                    //if (IsProcessRunningWithWindow(processName))
                    //{
                    //    //Console.WriteLine($"{processName} is running with main window");
                    //}
                    //else
                    //{
                    //    //Console.WriteLine($"{processName} is NOT running with main window。");
                    //    Thread.Sleep(315);
                    //    Application.Current.Dispatcher.Invoke(() =>
                    //    {
                    //        Application.Current.Shutdown();
                    //    });
                    //}


                    // ============================================================
                    // 【核心修改：管道连接与重连逻辑】
                    // ============================================================
                    try
                    {
                        // 如果管道为空、已断开或未连接，尝试连接/重连
                        if (pipeClient == null || !pipeClient.IsConnected)
                        {
                            // 清理旧的管道对象
                            if (pipeClient != null)
                            {
                                pipeClient.Dispose();
                                pipeClient = null;
                            }

                            // 创建新的管道客户端
                            // "." 代表本机，"MyGameCommPipe" 必须与 C++ 定义一致
                            pipeClient = new NamedPipeClientStream(".", "F9FAA61C-A540-15C5-5668-E5C9D66D4AB6", PipeDirection.Out);

                            // 尝试连接，设置 1 秒超时
                            // 注意：如果 C++ 服务端刚启动，这里可能需要一点时间，如果连不上就下次循环重试
                            pipeClient.Connect(1000);

                            Console.WriteLine("Connected to Game!");
                        }

                        // ============================================================
                        // 【核心修改：数据发送逻辑】
                        // ============================================================
                        if (pipeClient.IsConnected)
                        {
                            string message = Dispatcher.Invoke(() => ReadAdvancedSettings(newValue));
                            byte[] data = Encoding.UTF8.GetBytes(message);
                            pipeClient.Write(data, 0, data.Length);

                        }
                    }
                    catch (Exception ex)
                    {
                        // 捕获异常（连接失败或写入失败）
                        Console.WriteLine($"Pipe Error: {ex.Message}");

                        // 发生错误时，强制销毁当前管道对象，下次循环会尝试重建
                        if (pipeClient != null)
                        {
                            pipeClient.Dispose();
                            pipeClient = null;
                        }
                    }

                    // fps值发送逻辑保持不变，继续在循环中持续更新和发送数据
                    try
                    {
                        // 如果管道为空、已断开或未连接，尝试连接/重连
                        if (pipeClientFPS == null || !pipeClientFPS.IsConnected)
                        {
                            // 清理旧的管道对象
                            if (pipeClientFPS != null)
                            {
                                pipeClientFPS.Dispose();
                                pipeClientFPS = null;
                            }

                            // 创建新的管道客户端
                            // "." 代表本机，"MyGameCommPipe" 必须与 C++ 定义一致
                            pipeClientFPS = new NamedPipeClientStream(".", "55984705-F24C-45C2-B2B7-27F047B43A56", PipeDirection.Out);

                            // 尝试连接，设置 1 秒超时
                            // 注意：如果 C++ 服务端刚启动，这里可能需要一点时间，如果连不上就下次循环重试
                            pipeClientFPS.Connect(1000);

                            Console.WriteLine("Connected to Game!");
                        }

                        // ============================================================
                        // 【核心修改：数据发送逻辑】
                        // ============================================================
                        if (pipeClientFPS.IsConnected)
                        {
                            string message = Dispatcher.Invoke(() => ReadAdvancedSettings(newValue));
                            byte[] data = Encoding.UTF8.GetBytes(message);
                            pipeClientFPS.Write(data, 0, data.Length);

                        }
                    }
                    catch (Exception ex)
                    {
                        // 捕获异常（连接失败或写入失败）
                        Console.WriteLine($"Pipe Error: {ex.Message}");

                        // 发生错误时，强制销毁当前管道对象，下次循环会尝试重建
                        if (pipeClientFPS != null)
                        {
                            pipeClientFPS.Dispose();
                            pipeClientFPS = null;
                        }
                    }

                }
            }
            catch (Exception)
            {
                //if (Application.Current != null)
                //{
                //    Task.Run(() =>
                //    {
                //        Application.Current.Dispatcher.Invoke(() =>
                //        {
                //            MessageBox.Show("Could not unlock the FPS" + Environment.NewLine +
                //"The reason might be:" + Environment.NewLine +
                //"1. Wuthering Waves was not closed correctly" + Environment.NewLine +
                //"2. The user selected the wrong Wuthering Waves EXE file" + Environment.NewLine +
                //"3. Game version update causes memory address changes that cannot be modified",
                //"Notification",
                //MessageBoxButton.OK,
                //MessageBoxImage.Warning);

                //            // Show the window
                //            this.Show();
                //            this.WindowState = WindowState.Normal;
                //        });
                //    });
                //}
            }

        detectgameservererror:
            {
                //if (Application.Current != null)
                //{
                //    Application.Current.Dispatcher.Invoke(() =>
                //    {
                //        // Show the window
                //        this.Show();
                //        this.WindowState = WindowState.Normal;
                //        start_but.IsEnabled = true;
                //        start_but.Content = "Start Game";
                //    });
                //    return;
                //}
            }
        }

        private Advancelaunch advancelaunchWindow; // 声明Advancelaunch窗口的引用
        private CancellationTokenSource _cts; // 声明取消令牌源
        private void Advance_Launch(object sender, RoutedEventArgs e)
        {

            IniData data = null; // 在try块外部声明变量
            try
            {
                var parser = new FileIniDataParser();
                //var data = parser.ReadFile(ConfigFileName);
                data = parser.ReadFile(ConfigFileName);
            }
            catch
            {
                MessageBox.Show("INI file not exist", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }
            string GameServerA = data[Section][OptionKey5];
            GameServerAvalue = int.Parse(GameServerA);


            const string processNametest = "Client-Win64-Shipping";
            try
            {
                var processes = Process.GetProcessesByName(processNametest);
                if (processes.Length > 0)
                {
                    Console.WriteLine($"{processNametest} game is already running");
                    MessageBox.Show("Game is already running", "Notification", MessageBoxButton.OK, MessageBoxImage.Information);
                    return;
                }
            }
            catch (OperationCanceledException)
            {

            }


            //Advancelaunch windowab = new Advancelaunch();
            //// 设置模态窗口的Owner属性为主窗口
            //windowab.Owner = this;
            // 替换局部变量，使用类成员变量
            advancelaunchWindow = new Advancelaunch();
            // 设置模态窗口的Owner属性为主窗口
            advancelaunchWindow.Owner = this;
            advancelaunchWindow.Topmost = true;
            // 保存主窗口的正常透明度
            double normal = this.Opacity;

            var parentTop = this.Top;
            var parentLeft = this.Left;
            var parentWidth = this.ActualWidth;
            var parentHeight = this.ActualHeight;
            double top = parentTop + (parentHeight - advancelaunchWindow.Height) / 2;
            double left = parentLeft + (parentWidth - advancelaunchWindow.Width) / 2;
            advancelaunchWindow.Top = top;
            advancelaunchWindow.Left = left;

            _cts = new CancellationTokenSource(); // 初始化取消令牌源
            // 启动监控任务时传递取消令牌
            Task.Run(() => MonitorProcess(_cts.Token));
            advancelaunchWindow.Closed += (s, args) =>
            {
                // 窗口关闭时触发取消操作
                //Console.WriteLine("Monitoring stopped.123");
                _cts.Cancel();
            };


            // 降低主窗口的透明度
            this.Opacity = 0.4;
            advancelaunchWindow.ShowDialog();
            // 模态窗口关闭后，恢复主窗口的正常透明度
            this.Opacity = normal;

        }

        private async Task MonitorProcess(CancellationToken token)
        {
            const string processNametest = "Client-Win64-Shipping";

            bool hasinjected = false; // 用于跟踪是否已经注入过DLL

            try
            {
                while (true)
                {
                    token.ThrowIfCancellationRequested(); // 检查是否被取消

                    var processes = Process.GetProcessesByName(processNametest);
                    if (processes.Length > 0)
                    {
                        Console.WriteLine($"{processNametest} is running");
                        var process = processes[0];  // 这里声明 process 变量

                        if (!process.HasExited && !hasinjected)
                        {
                            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                            string dllToInject = Path.Combine(baseDir, @"ulk_ww_tools\ww_ulk_plugin.dll");
                            string dllToInject_fps = Path.Combine(baseDir, @"ulk_ww_tools\ww_ulk_plugin_base.dll");


                            // 注入fpsDLL 26011
                            List<string> dlls = null;
                            dlls = new List<string> { dllToInject_fps };

                            //修复：在UI线程上获取cb_advanced的状态 260611
                            bool shouldinject = false;
                            Application.Current.Dispatcher.Invoke(() =>
                            {
                                shouldinject = cb_advanced.IsChecked == true;
                            });

                            if (shouldinject)
                            {
                                dlls.Add(dllToInject);
                            }

                            InjectOnly(processNametest, dlls);

                            hasinjected = true; // 标记为已注入
                        }

                        //if (!process.HasExited && process.MainWindowHandle != IntPtr.Zero)
                        if (!process.HasExited)
                        {
                            Console.WriteLine("123mainwin");
                            Task.Run(() => Unlockstart_test(true));
                            //Application.Current.Dispatcher.Invoke(() =>
                            //{
                            //    start_but.IsEnabled = false;
                            //    start_but.Content = "Advac Mode";

                            //    //关闭Advancelaunch
                            //    if (advancelaunchWindow != null)
                            //    {
                            //        Thread.Sleep(1000);
                            //        advancelaunchWindow.Close(); // 关闭Advancelaunch窗口
                            //        advancelaunchWindow = null;
                            //    }
                            //});
                            // 在 UI 线程异步执行关闭操作
                            await Application.Current.Dispatcher.InvokeAsync(async () =>
                            {
                                start_but.IsEnabled = false;
                                start_but.Content = "Manual Mo...";

                                if (advancelaunchWindow != null)
                                {
                                    await Task.Delay(1531);
                                    advancelaunchWindow.UpdateTipMessage("Game detected!"); // 调用窗口的公共方法
                                    // 异步等待 1 秒（不阻塞 UI）
                                    await Task.Delay(1531);

                                    // 关闭前增加安全性检查
                                    if (advancelaunchWindow.IsVisible)
                                    {
                                        advancelaunchWindow.Close();
                                    }
                                    advancelaunchWindow = null;
                                }
                            });
                            break;
                        }
                    }
                    else
                    {
                        Console.WriteLine($"{processNametest} NOT launch");
                    }

                    // 使用可取消的异步延迟
                    await Task.Delay(150, token);
                }
            }
            catch (OperationCanceledException)
            {
                Console.WriteLine("Monitoring stopped.");
            }
        }


        private void Tb_main_ValueChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            if (Tb_main.Value.HasValue)
            {
                sli_main.Value = Tb_main.Value.Value;
            }
        }

        private void Tb_fov_ValueChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            if (Tb_fov.Value.HasValue)
            {
                sli_fov.Value = Tb_fov.Value.Value;
            }
        }

        private void Slider_ValueChanged_fov(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            var value = (sender as Slider).Value;

            Tb_fov.Text = value.ToString("F1"); // 保留1位小数

            if (notifyIcon != null)
            {
                notifyIcon.Text = "WW unlocker (FOV: " + Tb_fov.Text + ")";
            }
        }

        private string LoadGenshinToolsDLL(string resourceFileName, string outputFileName)
        {
            // 构建资源完整名称和输出文件路径
            string resourceName = $"ww_unlockfps.Resources.{resourceFileName}";
            string filePath = Path.Combine(AppContext.BaseDirectory, "ulk_ww_tools", outputFileName);

            //// 检查文件是否已存在
            //if (File.Exists(filePath))
            //    return filePath;

            var assembly = Assembly.GetExecutingAssembly();

            // 验证资源是否存在（调试时可启用打印）
            // Console.WriteLine("可用的嵌入资源:");
            // foreach (var name in assembly.GetManifestResourceNames())
            // {
            //     Console.WriteLine(name);
            // }

            if (!assembly.GetManifestResourceNames().Contains(resourceName))
                throw new Exception($"资源 '{resourceName}' 未找到");

            // 确保输出目录存在
            string outputDir = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrEmpty(outputDir) && !Directory.Exists(outputDir))
            {
                Directory.CreateDirectory(outputDir);
            }

            // 从嵌入资源提取并保存文件
            using (var stream = assembly.GetManifestResourceStream(resourceName))
            {
                if (stream == null)
                    throw new Exception($"无法加载资源 '{resourceName}'");

                try
                {
                    using (var fileStream = new FileStream(filePath, FileMode.Create, FileAccess.Write))
                    {
                        stream.CopyTo(fileStream);
                    }
                }
                catch (Exception ex)
                {
                    // 截断异常信息以避免过长
                    string message = ex.Message;
                    const int maxLength = 500;
                    if (message.Length > maxLength)
                    {
                        message = message.Substring(0, maxLength) + "...";
                    }
                    throw new Exception($"DLL load error: {message}");
                }
            }

            return filePath;
        }

        public string ReadAdvancedSettings(float fpsvalue = 150.0f)
        {
            double fov = sli_fov.Value;

            // 260611 获取FPS的值
            //int fps = Tb_main != null ? (int)Tb_main.Value : 150;
            int fps = (int)fpsvalue; // 默认值

            int advanced = cb_advanced.IsChecked == true ? 1 : 0;
            int enableFov = check_fov.IsChecked == true ? 1 : 0;
            int hideUid = check_hideuid.IsChecked == true ? 1 : 0;
            int clearCache = 0;

            // 260611 构建消息字符串，包含FPS和FOV值
            string message = $"{advanced},{enableFov},{fov:F1},{hideUid},{clearCache},{fps}";

            return message;
        }
    }
}