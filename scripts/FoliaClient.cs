using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.IO;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text;
using System.Threading;
namespace AtkBattery {
    internal static class FoliaModes {
        internal static readonly string[] Keys={"compact","controls"};
        internal static readonly string[] Names={"紧凑开关（方案 A）","常驻控制（方案 B）"};
        internal static string Normalize(string value){return value=="compact"?"compact":"controls";}
        internal static string Name(string value){return Normalize(value)=="compact"?Names[0]:Names[1];}
        internal static int TaskbarModeValue(string value){return Normalize(value)=="compact"?1:2;}
    }
    [DataContract] internal sealed class FoliaBridgeInfo {
        [DataMember(Name="pid")] public int Pid;
        [DataMember(Name="port")] public int Port;
        [DataMember(Name="token")] public string Token;
        [DataMember(Name="startedAt")] public long StartedAt;
    }
    [DataContract] internal sealed class FoliaState {
        [DataMember(Name="connected")] public bool Connected;
        [DataMember(Name="clientAlive")] public bool ClientAlive;
        [DataMember(Name="state")] public string State;
        [DataMember(Name="title")] public string Title;
        [DataMember(Name="artist")] public string Artist;
        [DataMember(Name="album")] public string Album;
        [DataMember(Name="coverUrl")] public string CoverUrl;
        [DataMember(Name="position")] public double Position;
        [DataMember(Name="duration")] public double Duration;
        [DataMember(Name="liked")] public bool Liked;
        [DataMember(Name="canLike")] public bool CanLike;
        [DataMember(Name="volume")] public double Volume;
        [DataMember(Name="muted")] public bool Muted;
        [DataMember(Name="loop")] public string Loop;
        [DataMember(Name="currentLyric")] public string CurrentLyric;
        [DataMember(Name="nextLyric")] public string NextLyric;
        [DataMember(Name="updatedAt")] public long UpdatedAt;
        internal bool IsPlaying {get{return String.Equals(State,"playing",StringComparison.OrdinalIgnoreCase);}}
        internal int VolumePercent {get{return Math.Max(0,Math.Min(100,(int)Math.Round(Volume*100.0)));}}
        internal string LoopLabel {get{return Loop=="one"?"单曲循环":Loop=="all"?"列表循环":"顺序播放";}}
        internal string Signature {get{return (Connected?"1":"0")+"|"+(ClientAlive?"1":"0")+"|"+(State??"")+"|"+(Title??"")+"|"+(Artist??"")+"|"+(Liked?"1":"0")+"|"+(Muted?"1":"0")+"|"+(Loop??"")+"|"+VolumePercent+"|"+((int)Math.Round(Position))+"|"+((int)Math.Round(Duration))+"|"+(CurrentLyric??"")+"|"+(NextLyric??"");}}
    }
    [DataContract] internal sealed class FoliaQueueItem {
        [DataMember(Name="index")] public int Index;
        [DataMember(Name="title")] public string Title;
        [DataMember(Name="artist")] public string Artist;
        [DataMember(Name="current")] public bool Current;
    }
    [DataContract] internal sealed class FoliaQueueSnapshot {
        [DataMember(Name="items")] public FoliaQueueItem[] Items;
        [DataMember(Name="total")] public int Total;
        [DataMember(Name="currentIndex")] public int CurrentIndex;
    }
    [DataContract] internal sealed class FoliaLyricLine {
        [DataMember(Name="time")] public double Time;
        [DataMember(Name="text")] public string Text;
        [DataMember(Name="subText")] public string SubText;
    }
    [DataContract] internal sealed class FoliaLyricsSnapshot {
        [DataMember(Name="lines")] public FoliaLyricLine[] Lines;
    }
    [DataContract] internal sealed class FoliaPlaylistItem {
        [DataMember(Name="id")] public string Id;
        [DataMember(Name="name")] public string Name;
        [DataMember(Name="trackCount")] public int TrackCount;
        [DataMember(Name="liked")] public bool Liked;
        [DataMember(Name="coverUrl")] public string CoverUrl;
    }
    [DataContract] internal sealed class FoliaPlaylistsSnapshot {
        [DataMember(Name="items")] public FoliaPlaylistItem[] Items;
        [DataMember(Name="error")] public string Error;
    }
    internal static class FoliaBridgeInstaller {
        internal const string ModId="myfinder-folia-bridge";
        static readonly string[] ResourceNames={"FoliaBridgeModJson","FoliaBridgeMainCjs","FoliaBridgeClientCjs"};
        static readonly string[] FileNames={"mod.json","main.cjs","client.mjs"};
        internal static string FoliaAppDataDirectory {
            get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),"Folia");}
        }
        internal static string BridgeModDirectory {
            get{return Path.Combine(FoliaAppDataDirectory,"mods",ModId);}
        }
        internal static bool IsFoliaInstalled() {
            try{return Directory.Exists(FoliaAppDataDirectory);}catch{return false;}
        }
        internal static bool IsBridgeInstalled() {
            try{
                string dir=BridgeModDirectory;
                foreach(string name in FileNames)if(!File.Exists(Path.Combine(dir,name)))return false;
                return true;
            }catch{return false;}
        }
        internal static bool IsBridgeEnabledInFolia() {
            try{
                string path=Path.Combine(FoliaAppDataDirectory,"mod-system.json");
                if(!File.Exists(path))return false;
                string text=File.ReadAllText(path,Encoding.UTF8);
                int idx=text.IndexOf("\""+ModId+"\"",StringComparison.Ordinal);
                if(idx<0)return false;
                string slice=text.Substring(idx,Math.Min(300,text.Length-idx));
                return slice.Contains("\"enabled\": true")||slice.Contains("\"enabled\":true");
            }catch{return false;}
        }
        internal static bool EnsureBridgeInstalled() {
            if(!IsFoliaInstalled())return false;
            try{
                string dir=BridgeModDirectory;
                Directory.CreateDirectory(dir);
                var asm=typeof(FoliaBridgeInstaller).Assembly;
                for(int i=0;i<ResourceNames.Length;i++){
                    using(var stream=asm.GetManifestResourceStream(ResourceNames[i])){
                        if(stream==null)return false;
                        byte[] expected;
                        using(var ms=new MemoryStream()){stream.CopyTo(ms);expected=ms.ToArray();}
                        string target=Path.Combine(dir,FileNames[i]);
                        bool same=false;
                        if(File.Exists(target)){
                            byte[] current=File.ReadAllBytes(target);
                            if(current.Length==expected.Length){
                                same=true;
                                for(int j=0;j<current.Length;j++)if(current[j]!=expected[j]){same=false;break;}
                            }
                        }
                        if(!same)File.WriteAllBytes(target,expected);
                    }
                }
                return true;
            }catch{return false;}
        }
    }
    internal sealed class FoliaClient : IDisposable {
        readonly Action onChanged;
        readonly object gate=new object();
        [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hWnd,int nCmdShow);
        [DllImport("user32.dll")] static extern bool IsIconic(IntPtr hWnd);
        [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr hWnd);
        volatile bool disposed;
        int polling;
        FoliaState state;
        FoliaQueueSnapshot queue;
        FoliaLyricsSnapshot lyrics;
        FoliaPlaylistsSnapshot playlists;
        readonly Dictionary<string,Image> coverCache=new Dictionary<string,Image>();
        readonly HashSet<string> coverPending=new HashSet<string>();
        bool foliaRunning;
        DateTime lastProcessCheck=DateTime.MinValue;
        DateTime lastDeployAttempt=DateTime.MinValue;
        internal FoliaClient(Action changedCallback) {
            onChanged=changedCallback;
        }
        internal bool FoliaRunning {get{lock(gate)return foliaRunning;}}
        internal bool Connected {get{lock(gate)return foliaRunning&&state!=null&&state.Connected;}}
        internal FoliaState State {get{lock(gate)return state;}}
        internal FoliaQueueSnapshot Queue {get{lock(gate)return queue;}}
        internal FoliaLyricsSnapshot Lyrics {get{lock(gate)return lyrics;}}
        internal FoliaPlaylistsSnapshot Playlists {get{lock(gate)return playlists;}}
        internal int CoverCacheCount {get{lock(gate)return coverCache.Count;}}
        internal Image GetCoverImage(string url) {
            if(disposed||String.IsNullOrEmpty(url))return null;
            lock(gate){
                Image cached;
                if(coverCache.TryGetValue(url,out cached))return cached;
                if(coverPending.Contains(url))return null;
                if(!url.StartsWith("http://",StringComparison.OrdinalIgnoreCase)&&!url.StartsWith("https://",StringComparison.OrdinalIgnoreCase))return null;
                coverPending.Add(url);
            }
            ThreadPool.QueueUserWorkItem(delegate {
                Image thumb=null;
                try{
                    try{ServicePointManager.SecurityProtocol|=(SecurityProtocolType)3072;}catch{}
                    string reqUrl=url;
                    if(reqUrl.IndexOf("126.net",StringComparison.OrdinalIgnoreCase)>=0&&reqUrl.IndexOf('?')<0)reqUrl+="?param=96y96";
                    var req=(HttpWebRequest)WebRequest.Create(reqUrl);
                    req.Timeout=3500;req.ReadWriteTimeout=3500;
                    using(var resp=(HttpWebResponse)req.GetResponse())
                    using(var stream=resp.GetResponseStream()){
                        if(stream!=null){
                            using(var raw=Image.FromStream(stream)){
                                var bmp=new Bitmap(64,64);
                                using(var g=Graphics.FromImage(bmp)){
                                    g.InterpolationMode=InterpolationMode.HighQualityBicubic;
                                    g.SmoothingMode=SmoothingMode.AntiAlias;
                                    g.DrawImage(raw,0,0,64,64);
                                }
                                thumb=bmp;
                            }
                        }
                    }
                }catch{}
                bool updated=false;
                lock(gate){
                    coverPending.Remove(url);
                    if(thumb!=null&&!disposed){
                        Image old;
                        if(coverCache.TryGetValue(url,out old)&&old!=null)old.Dispose();
                        coverCache[url]=thumb;
                        updated=true;
                    }else if(thumb!=null){thumb.Dispose();}
                }
                if(updated&&!disposed&&onChanged!=null)onChanged();
            });
            return null;
        }
        internal static string BridgeInfoPath {
            get{return Path.Combine(DeviceSettings.DataDirectory,"folia-bridge.json");}
        }
        static bool DetectFoliaProcess() {
            try{return Process.GetProcessesByName("Folia").Length>0;}catch{return false;}
        }
        internal static FoliaBridgeInfo ReadBridgeInfo() {
            try{
                string path=BridgeInfoPath;
                if(!File.Exists(path))return null;
                using(var stream=File.OpenRead(path)){
                    var ser=new DataContractJsonSerializer(typeof(FoliaBridgeInfo));
                    var info=ser.ReadObject(stream) as FoliaBridgeInfo;
                    if(info==null||info.Port<=0||info.Port>65535||String.IsNullOrEmpty(info.Token))return null;
                    return info;
                }
            }catch{return null;}
        }
        static T GetJson<T>(FoliaBridgeInfo info,string path,int timeoutMs=900) where T : class {
            var req=(HttpWebRequest)WebRequest.Create("http://127.0.0.1:"+info.Port.ToString(CultureInfo.InvariantCulture)+path);
            req.Method="GET";
            req.Timeout=timeoutMs;
            req.ReadWriteTimeout=timeoutMs;
            req.Proxy=null;
            req.Headers["X-MyFinder-Token"]=info.Token;
            using(var resp=(HttpWebResponse)req.GetResponse())
            using(var stream=resp.GetResponseStream()){
                if(stream==null)return null;
                var ser=new DataContractJsonSerializer(typeof(T));
                return ser.ReadObject(stream) as T;
            }
        }
        static bool PostCommand(FoliaBridgeInfo info,string action,string valueJson) {
            var req=(HttpWebRequest)WebRequest.Create("http://127.0.0.1:"+info.Port.ToString(CultureInfo.InvariantCulture)+"/cmd");
            req.Method="POST";
            req.ContentType="application/json; charset=utf-8";
            req.Timeout=1200;
            req.ReadWriteTimeout=1200;
            req.Proxy=null;
            req.ServicePoint.Expect100Continue=false;
            req.Headers["X-MyFinder-Token"]=info.Token;
            string escapedAction=action.Replace("\\","\\\\").Replace("\"","\\\"");
            string body=valueJson==null?"{\"action\":\""+escapedAction+"\"}":"{\"action\":\""+escapedAction+"\",\"value\":"+valueJson+"}";
            byte[] bytes=Encoding.UTF8.GetBytes(body);
            req.ContentLength=bytes.Length;
            using(var stream=req.GetRequestStream())stream.Write(bytes,0,bytes.Length);
            using(var resp=(HttpWebResponse)req.GetResponse())return (int)resp.StatusCode==200;
        }
        internal void PollAsync(bool forceProcessCheck=false) {
            if(disposed)return;
            if(Interlocked.CompareExchange(ref polling,1,0)!=0)return;
            ThreadPool.QueueUserWorkItem(delegate {
                try{PollCore(forceProcessCheck);}
                finally{Interlocked.Exchange(ref polling,0);}
            });
        }
        void PollCore(bool forceProcessCheck) {
            if(disposed)return;
            var now=DateTime.UtcNow;
            bool prevRunning;
            lock(gate){prevRunning=foliaRunning;}
            bool running=prevRunning;
            if(forceProcessCheck||(now-lastProcessCheck).TotalSeconds>=2){
                lastProcessCheck=now;
                running=DetectFoliaProcess();
            }
            if((running&&!prevRunning)||(now-lastDeployAttempt).TotalMinutes>=10){
                lastDeployAttempt=now;
                if(FoliaBridgeInstaller.IsFoliaInstalled())FoliaBridgeInstaller.EnsureBridgeInstalled();
            }
            FoliaState nextState=null;
            if(running){
                var info=ReadBridgeInfo();
                if(info!=null){
                    try{nextState=GetJson<FoliaState>(info,"/state");}
                    catch{nextState=null;}
                }
            }
            bool changed=false;
            lock(gate){
                string prevSig=(foliaRunning?"1":"0")+"|"+(state==null?"":state.Signature);
                string nextSig=(running?"1":"0")+"|"+(nextState==null?"":nextState.Signature);
                foliaRunning=running;
                state=nextState;
                if(!running){queue=null;lyrics=null;}
                changed=prevSig!=nextSig;
            }
            if(changed&&!disposed&&onChanged!=null)onChanged();
        }
        internal void SendCommand(string action,string rawJsonValue=null) {
            if(disposed||String.IsNullOrEmpty(action))return;
            ThreadPool.QueueUserWorkItem(delegate {
                var info=ReadBridgeInfo();
                if(info==null)return;
                try{
                    PostCommand(info,action,rawJsonValue);
                    Thread.Sleep(140);
                    PollCore(true);
                }catch{}
            });
        }
        internal void OpenMainWindow() {
            if(disposed)return;
            ThreadPool.QueueUserWorkItem(delegate {
                try{
                    string exePath=null;
                    bool activated=false;
                    var procs=Process.GetProcessesByName("Folia");
                    foreach(var proc in procs){
                        using(proc){
                            if(activated)continue;
                            try{
                                IntPtr hwnd=proc.MainWindowHandle;
                                if(hwnd!=IntPtr.Zero){
                                    ShowWindow(hwnd,IsIconic(hwnd)?9:5);
                                    BringWindowToTop(hwnd);
                                    SetForegroundWindow(hwnd);
                                    activated=true;
                                    continue;
                                }
                                if(String.IsNullOrEmpty(exePath)&&proc.MainModule!=null)exePath=proc.MainModule.FileName;
                            }catch{}
                        }
                    }
                    if(activated)return;
                    if(String.IsNullOrEmpty(exePath)){
                        string candidate=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Programs","Folia","Folia.exe");
                        if(File.Exists(candidate))exePath=candidate;
                    }
                    if(!String.IsNullOrEmpty(exePath)&&File.Exists(exePath)){
                        Process.Start(new ProcessStartInfo(exePath){UseShellExecute=true});
                    }
                }catch{}
            });
        }
        internal void RefreshDetailsAsync() {
            if(disposed)return;
            ThreadPool.QueueUserWorkItem(delegate {
                var info=ReadBridgeInfo();
                if(info==null)return;
                bool changed=false;
                try{
                    var q=GetJson<FoliaQueueSnapshot>(info,"/queue",1200);
                    var l=GetJson<FoliaLyricsSnapshot>(info,"/lyrics",1200);
                    lock(gate){
                        queue=q??queue;
                        lyrics=l??lyrics;
                        changed=true;
                    }
                }catch{}
                if(changed&&!disposed&&onChanged!=null)onChanged();
            });
        }
        internal void RefreshPlaylistsAsync() {
            if(disposed)return;
            ThreadPool.QueueUserWorkItem(delegate {
                var info=ReadBridgeInfo();
                if(info==null){
                    lock(gate){playlists=new FoliaPlaylistsSnapshot{Items=new FoliaPlaylistItem[0],Error="未检测到 Folia 桥接服务"};}
                    if(!disposed&&onChanged!=null)onChanged();
                    return;
                }
                FoliaPlaylistsSnapshot next=null;
                try{
                    bool sent=PostCommand(info,"refreshPlaylists",null);
                    if(sent)Thread.Sleep(260);
                    var p=GetJson<FoliaPlaylistsSnapshot>(info,"/playlists",1500);
                    next=p??new FoliaPlaylistsSnapshot{Items=new FoliaPlaylistItem[0],Error="请重启 Folia 使新版桥接模组生效"};
                }catch{
                    next=new FoliaPlaylistsSnapshot{Items=new FoliaPlaylistItem[0],Error="请重启 Folia 使新版桥接模组生效"};
                }
                lock(gate){playlists=next;}
                if(!disposed&&onChanged!=null)onChanged();
            });
        }
        internal void AddToPlaylist(string playlistId,bool isLikedPlaylist) {
            if(disposed||String.IsNullOrEmpty(playlistId))return;
            string escaped=playlistId.Replace("\\","\\\\").Replace("\"","\\\"");
            string rawJson="{\"playlistId\":\""+escaped+"\",\"liked\":"+(isLikedPlaylist?"true":"false")+"}";
            SendCommand("addToPlaylist",rawJson);
        }
        internal static string FormatTime(double seconds) {
            if(Double.IsNaN(seconds)||Double.IsInfinity(seconds)||seconds<0)seconds=0;
            int total=(int)Math.Floor(seconds);
            return (total/60)+":"+(total%60).ToString("D2",CultureInfo.InvariantCulture);
        }
        public void Dispose() {
            disposed=true;
            lock(gate){
                foreach(var img in coverCache.Values)if(img!=null)img.Dispose();
                coverCache.Clear();
                coverPending.Clear();
            }
        }
    }
}
