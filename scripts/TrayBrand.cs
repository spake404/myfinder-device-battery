using System;
using System.Drawing;
using System.Runtime.InteropServices;

namespace AtkBattery {
    internal static class TrayBrand {
        internal const string Automatic="auto", Off="off";
        internal static readonly string[] Keys={Off,"razer","logitech","steelseries","corsair","hyperx","rog","zowie","playstation","xbox","samsung","apple","jabra","jbl","cooler-master","steam","redragon"};
        static readonly string[] Names={"关闭","雷蛇 Razer","罗技 Logitech","SteelSeries","海盗船 Corsair","HyperX","ROG","ZOWIE","PlayStation","Xbox","三星 Samsung","苹果 Apple","Jabra","JBL","Cooler Master","Steam","Redragon"};

        internal static string Normalize(string value){return Array.IndexOf(Keys,value)>=0?value:Automatic;}
        internal static string Name(string value){int index=Array.IndexOf(Keys,value);return index<0?"自动识别":Names[index];}
        internal static int DeviceKind(string kind) {
            switch(kind) {
                case "mouse":return 1;
                case "headset":return 2;
                case "keyboard":return 3;
                case "gamepad":case "controller":return 4;
                case "phone":return 5;
                case "microphone":return 6;
                default:return 0;
            }
        }

        [DllImport("MyFinderTaskbar.dll",CharSet=CharSet.Unicode,ExactSpelling=true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory|DllImportSearchPath.System32)]
        static extern int TaskbarDetectBrand(string provider,int vendorId,string name,int kind);
        [DllImport("MyFinderTaskbar.dll",ExactSpelling=true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory|DllImportSearchPath.System32)]
        static extern int TaskbarBrandColor(int accent,bool light);

        static bool unavailable;

        internal static int Resolve(string mode,string provider,int? vendorId,string name,string kind) {
            int manual=Array.IndexOf(Keys,mode);
            if(manual>=0)return manual;
            if(unavailable||String.IsNullOrEmpty(provider)&&!vendorId.HasValue&&String.IsNullOrEmpty(name))return 0;
            try {return TaskbarDetectBrand((provider??"").ToLowerInvariant(),vendorId??0,name??"",DeviceKind(kind));}
            catch(DllNotFoundException){unavailable=true;}
            catch(EntryPointNotFoundException){unavailable=true;}
            catch(BadImageFormatException){unavailable=true;}
            return 0;
        }

        internal static Color? ColorFor(int accent,bool light) {
            if(unavailable||accent<=0||accent>=Keys.Length)return null;
            try {return ColorTranslator.FromWin32(TaskbarBrandColor(accent,light));}
            catch(DllNotFoundException){unavailable=true;}
            catch(EntryPointNotFoundException){unavailable=true;}
            catch(BadImageFormatException){unavailable=true;}
            return null;
        }
    }
}
