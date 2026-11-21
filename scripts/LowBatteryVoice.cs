using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using AtkBattery;

namespace AtkBatteryTray {
    internal static class LowBatteryVoice {
        internal const int ToggleId=234;
        const int PlainText=16;
        static readonly object Gate=new object();
        static readonly Queue<string> Pending=new Queue<string>();
        static bool speaking;

        internal static string Phrase(IEnumerable<string> deviceNames,int threshold) {
            var names=new List<string>();
            var seen=new HashSet<string>(StringComparer.Ordinal);
            foreach(string name in deviceNames) {
                string label=String.IsNullOrWhiteSpace(name)?"外设":name.Trim();
                if(seen.Add(label))names.Add(label);
                if(names.Count==6)break;
            }
            string level=threshold==10?"十":threshold==15?"十五":threshold==20?"二十":threshold.ToString(System.Globalization.CultureInfo.InvariantCulture);
            return names.Count==0?null:"警告，"+String.Join("、",names.ToArray())+"电量已降至百分之"+level+"或以下。";
        }

        internal static void Enqueue(IEnumerable<string> deviceNames,int threshold) {
            string phrase=Phrase(deviceNames,threshold);
            if(phrase==null)return;
            lock(Gate) {
                if(Pending.Count>=8)return;
                Pending.Enqueue(phrase);
                if(speaking)return;
                speaking=true;
                ThreadPool.QueueUserWorkItem(Drain);
            }
        }

        static void Drain(object unused) {
            while(true) {
                string phrase;
                lock(Gate) {
                    if(Pending.Count==0){speaking=false;return;}
                    phrase=Pending.Dequeue();
                }
                try {
                    var preferences=DeviceSettings.Current;
                    if(preferences.LowBatteryNotifications&&preferences.LowBatteryVoice&&NativeDisplay.MayNotify())Speak(phrase);
                } catch(Exception){SafeDiagnostic.Transition("low_battery_voice","unavailable");}
            }
        }

        static object Invoke(object instance,string name,BindingFlags flags,params object[] args) {
            return instance.GetType().InvokeMember(name,flags,null,instance,args);
        }

        static void Speak(string phrase) {
            object voice=Activator.CreateInstance(Type.GetTypeFromProgID("SAPI.SpVoice",true));
            try {
                PreferChinese(voice);
                Invoke(voice,"Speak",BindingFlags.InvokeMethod,phrase,PlainText);
            } finally {Release(voice);}
        }

        static void PreferChinese(object voice) {
            foreach(string language in new[]{"804","404","c04"}) {
                object voices=null,token=null;
                try {
                    voices=Invoke(voice,"GetVoices",BindingFlags.InvokeMethod,"Language="+language,"");
                    if(Convert.ToInt32(Invoke(voices,"Count",BindingFlags.GetProperty))==0)continue;
                    token=Invoke(voices,"Item",BindingFlags.InvokeMethod,0);
                    Invoke(voice,"Voice",BindingFlags.PutRefDispProperty,token);
                    return;
                } catch(COMException) {}
                  catch(TargetInvocationException) {}
                  catch(MissingMethodException) {}
                finally {Release(token);Release(voices);}
            }
        }

        static void Release(object instance) {
            if(instance!=null&&Marshal.IsComObject(instance))Marshal.FinalReleaseComObject(instance);
        }
    }
}
