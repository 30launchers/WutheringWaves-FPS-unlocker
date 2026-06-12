using IniParser;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Forms;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using IniParser;
using IniParser.Model;
using System.IO;

namespace ww_unlockfps
{
    /// <summary>
    /// Setup1.xaml 的交互逻辑
    /// </summary>
    public partial class Setup1 : Window
    {
        private const string ConfigFileName = "ww_fps_config.ini";
        private const string Section = "Settings";
        private const string InputKey2 = "PathValue";
        private const string OptionKey5 = "GameServerArea";
        private const string OptionKey6 = "GameParam";
        private const string OptionKey7 = "GameLaunchExe";


        public Setup1()
        {
            InitializeComponent();
            LoadPath();
            var _wpfDragHelper = new WpfDragHelper();
            this.SourceInitialized += (sender, e) =>
            {
                var helper = new WindowInteropHelper(this);
                _wpfDragHelper.HwndSource = HwndSource.FromHwnd(helper.Handle);
                _wpfDragHelper.AddHook();
                IconHelper.RemoveIcon(this);
            };
            this.Closing += (sender, e) =>
            {
                _wpfDragHelper.RemoveHook();
            };

            _wpfDragHelper.DragDrop += (sender, e) =>
            {
                var mousePosition = System.Windows.Forms.Control.MousePosition;
                var point = this.PointFromScreen(new System.Windows.Point(mousePosition.X, mousePosition.Y));

                if (IsMouseInDropArea(point))
                {
                    Path_EXE.Text = _wpfDragHelper.DropFilePaths[0];
                }
                else
                {
                    Mouse.OverrideCursor = System.Windows.Input.Cursors.No;
                    Task.Delay(315).Wait();
                    Mouse.OverrideCursor = null;
                }
            };
            //test按钮
            But_Clear.IsEnabled = false;
            GAME_SERV.IsEnabled = false;

        }

        private bool IsMouseInDropArea(System.Windows.Point point)
        {
            var position = DropArea.TransformToAncestor(this).Transform(new Point(0, 0));
            var width = DropArea.ActualWidth;
            var height = DropArea.ActualHeight;

            return point.X >= position.X && point.X <= position.X + width &&
                   point.Y >= position.Y && point.Y <= position.Y + height;
        }

        private void LoadPath()
        {
            try
            {
                var parser = new FileIniDataParser();
                var data = parser.ReadFile(ConfigFileName);
                Path_EXE.Text = data[Section][InputKey2];
                // 假设 data 是一个字典，Section 和 OptionKey5 是正确的键
                string optionValue = data[Section][OptionKey5];

                // 尝试将字符串转换为整数
                if (int.TryParse(optionValue, out int intValue))
                {
                    // 如果转换成功，则设置 ComboBox 的 SelectedIndex
                    GAME_SERV.SelectedIndex = intValue;
                }
                else
                {
                    // 如果转换失败，则处理错误情况，例如设置默认值或显示错误消息
                    // 例如，设置为 -1 或者第一个选项
                    GAME_SERV.SelectedIndex = -1; // 表示没有选中任何项
                                                  // 或者
                                                  // GAME_SERV.SelectedIndex = 0; // 默认选中第一个选项
                }
            }
            catch
            {
                System.Windows.MessageBox.Show("config error", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }



        private void SaveConfig()
        {
            try
            {
                var parser = new FileIniDataParser();
                var data = parser.ReadFile(ConfigFileName);
                data[Section][InputKey2] = Path_EXE.Text;
                data[Section][OptionKey5] = GAME_SERV.SelectedIndex.ToString();
                data[Section][OptionKey6] = Param_EXE.Text;
                data[Section][OptionKey7] = GAME_EXE_SELECT.SelectedIndex.ToString();
                parser.WriteFile(ConfigFileName, data);
            }
            catch 
            {
                System.Windows.MessageBox.Show("Save config error", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                System.Windows.Application.Current.Dispatcher.Invoke(() =>
                {
                    System.Windows.Application.Current.Shutdown();
                });
            }
        }


        public void UpdateTextParam(string textparam)
        {
            Param_EXE.Text = textparam;
        }

        public void UpdateGameSelect(int gamexeselect)
        {
            GAME_EXE_SELECT.SelectedIndex = gamexeselect;
        }


        //protected override void OnSourceInitialized(EventArgs e)
        //{
        //    IconHelper.RemoveIcon(this);
        //}


        private void Button_Clear(object sender, RoutedEventArgs e)
        {
            Path_EXE.Clear();
            Param_EXE.Clear();
        }

        private void Button_SELECT(object sender, RoutedEventArgs e)
        {
            var openFileDialog = new Microsoft.Win32.OpenFileDialog()
            {
                //Filter = "Text documents (.txt)|*.txt|All files (*.*)|*.*"
                Filter = "Executable files (*.exe)|*.exe|Wuthering Waves.exe|Wuthering Waves.exe",
                FilterIndex = 2
            };
            var result = openFileDialog.ShowDialog();
            if (result == true)
            {
                // 设置Path_EXE的Text属性为选择的文件路径
                Path_EXE.Text = openFileDialog.FileName;
            }
            else
            {
   
            }
        }

        private void EXE_file_Drop(object sender, System.Windows.DragEventArgs e)
        {
            if (e.Data.GetDataPresent(System.Windows.DataFormats.FileDrop))
            {
                string[] files = (string[])e.Data.GetData(System.Windows.DataFormats.FileDrop);

                if (files.Length > 1)
                {
                    System.Windows.MessageBox.Show("Please drag only one file", "Notification", MessageBoxButton.OK, MessageBoxImage.Information);
                    return;
                }

                string file = files[0];
                if (System.IO.Path.GetExtension(file).ToLower() != ".exe")
                {
                    System.Windows.MessageBox.Show("This is not the correct game EXE file", "Notification", MessageBoxButton.OK, MessageBoxImage.Information);
                    return;
                }

                if (System.IO.Path.GetFileName(file).ToLower() != "wuthering waves.exe")
                {
                    System.Windows.MessageBox.Show("This is not the correct game EXE file", "Notification", MessageBoxButton.OK, MessageBoxImage.Information);
                    return;
                }
                Path_EXE.Clear();
                Path_EXE.Text = file;
            }
        }

        private void Button_Confirm(object sender, RoutedEventArgs e)
        {
            string textBoxContent = Path_EXE.Text;

            if (textBoxContent.EndsWith("Wuthering Waves.exe", StringComparison.OrdinalIgnoreCase))
            {
                string directoryPath = System.IO.Path.GetDirectoryName(textBoxContent);
                string clientFolderPath = System.IO.Path.Combine(directoryPath, "Client");

                if (System.IO.File.Exists(textBoxContent))
                {
                    if (System.IO.Directory.Exists(clientFolderPath))
                    {
                        System.Windows.MessageBox.Show("Save Config successsfully", "Notification", MessageBoxButton.OK, MessageBoxImage.Information);
                        SaveConfig();
                        this.Close();
                    }
                    else
                    {
                        System.Windows.MessageBox.Show("That's not the right place", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                }
                else
                {
                    System.Windows.MessageBox.Show("The game EXE does not exist", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
            else
            {
                System.Windows.MessageBox.Show("Game EXE path is not right", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

    }
}