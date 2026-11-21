using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace AtkBatteryTray {
    internal sealed class TrayMenuDismissal : IDisposable {
        [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr window);
        [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] static extern short GetAsyncKeyState(int key);

        static readonly int[] MouseKeys={1,2,4,5,6};
        readonly ContextMenuStrip menu;
        readonly Timer watch=new Timer {Interval=15};
        IntPtr foregroundAtOpen;
        int previousButtons;

        internal TrayMenuDismissal(ContextMenuStrip menu) {
            this.menu=menu;
            menu.Opened+=Opened;
            menu.Closed+=Closed;
            menu.Disposed+=MenuDisposed;
            watch.Tick+=delegate {Observe(GetForegroundWindow(),PointerButtons(),Cursor.Position);};
        }

        static int PointerButtons() {
            int pressed=0;
            foreach(int key in MouseKeys)if((GetAsyncKeyState(key)&0x8000)!=0)pressed|=1<<key;
            return pressed;
        }

        void Opened(object sender,EventArgs args) {
            SetForegroundWindow(menu.Handle);
            if(!menu.Visible)return;
            Begin(GetForegroundWindow(),PointerButtons());
        }

        internal void Begin(IntPtr foreground,int buttons) {
            foregroundAtOpen=foreground;
            previousButtons=buttons;
            watch.Start();
        }

        void Closed(object sender,ToolStripDropDownClosedEventArgs args) {watch.Stop();}
        void MenuDisposed(object sender,EventArgs args) {Dispose();}

        internal void Observe(IntPtr foreground,int buttons,Point pointer) {
            if(!menu.Visible){watch.Stop();return;}
            bool newPress=(buttons&~previousButtons)!=0;
            previousButtons=buttons;
            bool switchedAway=foreground!=IntPtr.Zero&&foreground!=foregroundAtOpen;
            if(switchedAway&&InOpenMenuTree(menu,popup=>popup.IsHandleCreated&&popup.Handle==foreground)){
                foregroundAtOpen=foreground;
                switchedAway=false;
            }
            bool outsidePress=newPress&&!InOpenMenuTree(menu,popup=> {
                if(!popup.Bounds.Contains(pointer))return false;
                return popup.Region==null||popup.Region.IsVisible(popup.PointToClient(pointer));
            });
            if(switchedAway||outsidePress)
                menu.Close(outsidePress?ToolStripDropDownCloseReason.AppClicked:ToolStripDropDownCloseReason.AppFocusChange);
        }

        static bool InOpenMenuTree(ToolStripDropDown popup,Predicate<ToolStripDropDown> match) {
            if(!popup.Visible)return false;
            if(match(popup))return true;
            foreach(ToolStripItem item in popup.Items) {
                var submenu=item as ToolStripDropDownItem;
                if(submenu!=null&&submenu.HasDropDownItems&&InOpenMenuTree(submenu.DropDown,match))return true;
            }
            return false;
        }

        public void Dispose() {
            menu.Opened-=Opened;
            menu.Closed-=Closed;
            menu.Disposed-=MenuDisposed;
            watch.Dispose();
        }
    }
}
