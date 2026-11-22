using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using AtkBattery;

namespace AtkBatteryTray {
    internal sealed class TaskbarStrip : NativeWindow, IDisposable {
        const int ClickMessage = 0x804A;
        const string Library = "MyFinderTaskbar.dll";

        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        internal struct Reading {
            internal int Kind, Percent, Flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=8)] internal string Label;

            internal static Reading FromView(TrayView view, int warningThreshold) {
                var battery=view.Battery;
                int percent=battery.Percent??-1;
                return new Reading {
                    Kind=TrayBrand.DeviceKind(view.DeviceKind), Percent=percent, Label="",
                    Flags=(battery.Fresh&&!battery.Unconfirmed&&battery.Charging?1:0) |
                        (percent>=0&&(!battery.Fresh||battery.Unconfirmed)?2:0) |
                        (percent>=0&&battery.Fresh&&!battery.Unconfirmed&&percent<=warningThreshold?4:0) |
                        (battery.Approximate?8:0) | (view.BrandAccent<<8)
                };
            }
        }

        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        internal struct FoliaReading {
            internal int Mode;
            internal int Playing;
            internal int Liked;
            internal int LoopMode;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] internal string Title;
        }

        [DllImport(Library, ExactSpelling=true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory|DllImportSearchPath.System32)]
        static extern bool TaskbarCreate(IntPtr owner, uint message, bool light);
        [DllImport(Library, CharSet=CharSet.Unicode, ExactSpelling=true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory|DllImportSearchPath.System32)]
        static extern bool TaskbarUpdate([In] Reading[] readings, int count, string tooltip, bool light, uint badge);
        [DllImport(Library, ExactSpelling=true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory|DllImportSearchPath.System32)]
        static extern bool TaskbarSetFolia(ref FoliaReading folia);
        [DllImport(Library, ExactSpelling=true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory|DllImportSearchPath.System32)]
        static extern bool TaskbarReposition();
        [DllImport(Library, ExactSpelling=true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory|DllImportSearchPath.System32)]
        static extern void TaskbarDestroy();

        readonly Action<bool> visibility;
        readonly Action<int,Point> click;
        readonly Timer placement = new Timer { Interval=1000 };
        Reading[] readings=new Reading[0];
        FoliaReading folia;
        string tooltip, lastKey;
        bool active, unavailable, light;
        uint badge;

        internal TaskbarStrip(Action<bool> visibilityChanged, Action<int,Point> clicked) {
            visibility=visibilityChanged;
            click=clicked;
            CreateHandle(new CreateParams { Caption="MyFinder Device Battery Taskbar Bridge" });
            placement.Tick+=delegate { Synchronize(); };
        }

        internal static List<TrayView> VisibleViews(List<TrayView> views){return views.FindAll(view=>!view.Hidden);}
        internal void Update(bool enabled, List<TrayView> views, bool lightTheme, int warningThreshold, int foliaMode=0, FoliaState foliaState=null) {
            var visible=enabled&&!unavailable?VisibleViews(views):null;
            if(visible==null||(visible.Count==0&&foliaMode==0)) {
                placement.Stop();
                Reset();
                return;
            }
            var current=new List<Reading>();
            var tips=new List<string>();
            if(foliaMode>0) {
                string song=foliaState==null||String.IsNullOrEmpty(foliaState.Title)?"":(foliaState.Title+(String.IsNullOrEmpty(foliaState.Artist)?"":" - "+foliaState.Artist));
                if(song.Length>120)song=song.Substring(0,120);
                int loop=foliaState==null?0:(foliaState.Loop=="all"?1:(foliaState.Loop=="one"?2:0));
                folia=new FoliaReading {
                    Mode=foliaMode,
                    Playing=foliaState!=null&&foliaState.IsPlaying?1:0,
                    Liked=foliaState!=null&&foliaState.Liked?1:0,
                    LoopMode=loop,
                    Title=song,
                };
            } else {
                folia=new FoliaReading();
            }
            foreach(var view in visible) {
                current.Add(Reading.FromView(view,warningThreshold));
                tips.Add(view.Tooltip);
            }
            var kinds=new Dictionary<int,int>();
            foreach(var reading in current) {
                int count;
                kinds.TryGetValue(reading.Kind,out count);
                kinds[reading.Kind]=count+1;
            }
            var numbered=new Dictionary<int,int>();
            for(int index=0;index<current.Count;index++) {
                var reading=current[index];
                if(kinds[reading.Kind]<2)continue;
                int number;
                numbered.TryGetValue(reading.Kind,out number);
                numbered[reading.Kind]=++number;
                reading.Label=number.ToString();
                current[index]=reading;
            }
            readings=current.ToArray();
            tooltip=String.Join("\n",tips.ToArray());
            light=lightTheme;
            var primary=views[0];
            badge=primary.UpdateBadge?(uint)ColorTranslator.ToWin32(primary.UpdateColor):UInt32.MaxValue;
            Synchronize();
            if(!unavailable)placement.Start();
        }

        void Synchronize() {
            try {
                if(!active&&!TaskbarCreate(Handle,ClickMessage,light))return;
                string key=tooltip+"|"+light+"|"+badge+"|f:"+folia.Mode+":"+folia.Playing+":"+folia.Liked+":"+folia.LoopMode+":"+(folia.Title??"");
                foreach(var reading in readings)key+="|"+reading.Kind+":"+reading.Percent+":"+reading.Flags+":"+reading.Label;
                bool ready=true;
                if(key!=lastKey){
                    ready=TaskbarSetFolia(ref folia)&&TaskbarUpdate(readings,readings.Length,tooltip,light,badge);
                }else ready=TaskbarReposition();
                if(!ready) {
                    TaskbarDestroy();
                    lastKey=null;
                    SetActive(false);
                    return;
                }
                lastKey=key;
                SetActive(true);
            } catch(DllNotFoundException) { Disable(); }
              catch(EntryPointNotFoundException) { Disable(); }
              catch(BadImageFormatException) { Disable(); }
        }

        void SetActive(bool value) {
            if(active==value)return;
            active=value;
            visibility(value);
        }

        void Disable() {
            unavailable=true;
            placement.Stop();
            Reset();
        }

        internal void Reset() {
            if(active)TaskbarDestroy();
            lastKey=null;
            SetActive(false);
        }

        protected override void WndProc(ref Message message) {
            if(message.Msg==ClickMessage&&active) {
                int packed=unchecked((int)message.LParam.ToInt64());
                click(message.WParam.ToInt32(),new Point(unchecked((short)packed),unchecked((short)(packed>>16))));
                return;
            }
            base.WndProc(ref message);
        }

        public void Dispose() {
            placement.Dispose();
            Reset();
            DestroyHandle();
        }
    }
}
