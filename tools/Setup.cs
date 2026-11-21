using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using AtkBattery;

static class Setup {
    const string Title="MyFinder 设备电量安装程序";

    static string Version() {
        return ((AssemblyInformationalVersionAttribute)Attribute.GetCustomAttribute(typeof(Setup).Assembly,typeof(AssemblyInformationalVersionAttribute))).InformationalVersion;
    }

    static void WithPayload(string comparisonZip,Action<string> action) {
        string directory=Path.Combine(Path.GetTempPath(),"myfinder-setup-"+Guid.NewGuid().ToString("N"));
        using(var payload=typeof(Setup).Assembly.GetManifestResourceStream("MyFinderReleaseZip")) {
            if(payload==null)throw new InvalidDataException("安装包缺少程序文件。");
            if(comparisonZip!=null) {
                using(var hash=SHA256.Create()) {
                    string embedded=BitConverter.ToString(hash.ComputeHash(payload));
                    if(!embedded.Replace("-","").Equals(UpdatePackageVerifier.HashFile(comparisonZip),StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("安装程序与发行 ZIP 不一致。");
                }
                payload.Position=0;
            }
            UpdatePackageVerifier.ExtractAndValidateStream(payload,directory,Version(),CancellationToken.None);
            try {action(directory);}finally {UpdatePaths.DeleteTree(directory);}
        }
    }

    static void Install(string package) {
        string script=Path.Combine(package,"scripts","Install-Startup.ps1");
        var start=new ProcessStartInfo(Path.Combine(Environment.SystemDirectory,"WindowsPowerShell","v1.0","powershell.exe"),
            "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+script+"\" -FromSetup") {
            WorkingDirectory=package,UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden,RedirectStandardError=true
        };
        using(var process=Process.Start(start)) {
            string error=process.StandardError.ReadToEnd().Trim();process.WaitForExit();
            if(process.ExitCode!=0)throw new InvalidOperationException(error.Length==0?"安装未完成。退出代码："+process.ExitCode:error);
        }
    }

    [STAThread] static int Main(string[] args) {
        if(args.Length>0){
            if(args.Length!=2||args[0]!="--verify-payload")return 2;
            try{WithPayload(args[1],delegate{});return 0;}catch{return 1;}
        }
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        using(var mutex=new Mutex(false,"Local\\MyFinderDeviceBattery.Setup")) {
            try{if(!mutex.WaitOne(0)){MessageBox.Show("已有安装程序正在运行。",Title);return 1;}}catch(AbandonedMutexException){}
            try{
                if(MessageBox.Show("安装 MyFinder 设备电量 "+Version()+"？\n\n如程序正在运行，安装时会正常退出。现有配置与自启状态会保留。",Title,MessageBoxButtons.OKCancel,MessageBoxIcon.Question)!=DialogResult.OK)return 0;
                int result=1;
                using(var progress=new Form {Text=Title,ClientSize=new Size(350,82),FormBorderStyle=FormBorderStyle.FixedDialog,StartPosition=FormStartPosition.CenterScreen,ControlBox=false}) {
                    progress.Controls.Add(new Label {Text="正在校验并安装，请稍候…",Location=new Point(16,12),AutoSize=true});
                    progress.Controls.Add(new ProgressBar {Location=new Point(16,42),Size=new Size(318,16),Style=ProgressBarStyle.Marquee});
                    progress.Shown+=async delegate{
                        try{
                            await Task.Run(delegate{WithPayload(null,Install);});result=0;
                            MessageBox.Show(progress,"安装完成。可从开始菜单打开 MyFinder Device Battery。",Title,MessageBoxButtons.OK,MessageBoxIcon.Information);
                        }catch(Exception error){MessageBox.Show(progress,error.Message,Title,MessageBoxButtons.OK,MessageBoxIcon.Error);}
                        finally{progress.Close();}
                    };
                    Application.Run(progress);
                }
                return result;
            }finally{mutex.ReleaseMutex();}
        }
    }
}
