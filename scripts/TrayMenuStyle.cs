using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using AtkBattery;

namespace AtkBatteryTray {
    internal sealed class TrayMenuStyle : ToolStripRenderer {
        static readonly TrayMenuStyle Renderer=new TrayMenuStyle();
        static readonly Font StandardFont=new Font("Microsoft YaHei UI",9f);
        static readonly Font LargeFont=new Font("Microsoft YaHei UI",10.5f);

        internal static void Attach(ToolStripDropDownMenu menu) {
            menu.Renderer=Renderer;
            menu.ShowImageMargin=false;
            menu.ShowCheckMargin=false;
            menu.Opening+=delegate {ApplyLayout(menu);};
            menu.SizeChanged+=delegate {ApplyRegion(menu);};
            ApplyLayout(menu);
        }

        internal static void AttachSubmenu(ToolStripMenuItem item) {
            var menu=item.DropDown as ToolStripDropDownMenu;
            if(menu!=null)Attach(menu);
        }

        static float Scale(ToolStrip menu) {
            var popup=menu as ToolStripDropDown;
            if(popup!=null&&popup.OwnerItem!=null){
                var parent=popup.OwnerItem.GetCurrentParent();
                if(parent!=null)return Scale(parent);
            }
            return NativeDisplay.ScaleAt(menu.Visible?menu.Bounds.Location:Cursor.Position);
        }

        static void ApplyLayout(ToolStripDropDownMenu menu) {
            float scale=Scale(menu);
            menu.SuspendLayout();
            try {
                menu.Font=VisualDesign.LargeFont?LargeFont:StandardFont;
                menu.Padding=new Padding(0,(int)Math.Round(6*scale),0,(int)Math.Round(6*scale));
                int minimumWidth=(int)Math.Round(168*scale);
                foreach(ToolStripItem item in menu.Items){
                    if(!(item is ToolStripMenuItem))continue;
                    item.Padding=new Padding((int)Math.Round(12*scale),(int)Math.Round(5*scale),(int)Math.Round(44*scale),(int)Math.Round(5*scale));
                    int textWidth=TextRenderer.MeasureText(item.Text,item.Font,new Size(Int32.MaxValue,Int32.MaxValue),TextFormatFlags.SingleLine).Width;
                    minimumWidth=Math.Max(minimumWidth,textWidth+(int)Math.Round(64*scale));
                }
                menu.MinimumSize=new Size(Math.Min(minimumWidth,(int)Math.Round(360*scale)),0);
            } finally {menu.ResumeLayout(true);}
            ApplyRegion(menu);
        }

        static void ApplyRegion(ToolStrip menu) {
            var bounds=menu.ClientRectangle;
            if(bounds.Width<2||bounds.Height<2)return;
            Region next=null;
            if(!VisualDesign.HighContrast)
                using(var shape=VisualDesign.RoundRect(0,0,bounds.Width,bounds.Height,VisualDesign.PanelRadius*Scale(menu)))next=new Region(shape);
            var previous=menu.Region;
            menu.Region=next;
            if(previous!=null)previous.Dispose();
        }

        protected override void OnRenderToolStripBackground(ToolStripRenderEventArgs args) {
            using(var fill=new SolidBrush(ThemePalette.Current(Style.Dark).Card))args.Graphics.FillRectangle(fill,args.ToolStrip.ClientRectangle);
        }

        protected override void OnRenderToolStripBorder(ToolStripRenderEventArgs args) {
            var bounds=args.ToolStrip.ClientRectangle;
            if(bounds.Width<2||bounds.Height<2)return;
            bool dark=Style.Dark;
            var palette=ThemePalette.Current(dark);
            using(var pen=new Pen(palette.Border)) {
                if(VisualDesign.HighContrast){args.Graphics.DrawRectangle(pen,0,0,bounds.Width-1,bounds.Height-1);return;}
                var previous=args.Graphics.SmoothingMode;
                args.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
                using(var shape=VisualDesign.RoundRect(.5f,.5f,bounds.Width-1,bounds.Height-1,VisualDesign.PanelRadius*Scale(args.ToolStrip)))args.Graphics.DrawPath(pen,shape);
                args.Graphics.SmoothingMode=previous;
            }
        }

        protected override void OnRenderImageMargin(ToolStripRenderEventArgs args) {}

        protected override void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs args) {
            float scale=Scale(args.ToolStrip);
            var palette=ThemePalette.Current(Style.Dark);
            if(args.Item.Enabled&&(args.Item.Selected||args.Item.Pressed)){
                var bounds=new RectangleF(4*scale,scale,args.Item.Width-8*scale,args.Item.Height-2*scale);
                if(bounds.Width>0&&bounds.Height>0){
                    var previous=args.Graphics.SmoothingMode;
                    args.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
                    CompactRenderer.Fill(args.Graphics,bounds,palette.Interaction(args.Item.Pressed),VisualDesign.SelectionRadius*scale);
                    args.Graphics.SmoothingMode=previous;
                }
            }
            var item=args.Item as ToolStripMenuItem;
            if(item==null)return;
            bool isChecked=item.CheckState!=CheckState.Unchecked;
            var symbol=isChecked?"check":item.Tag as string;
            if(!String.IsNullOrEmpty(symbol)){
                var color=isChecked&&item.Enabled&&!VisualDesign.HighContrast?palette.Accent:Ink(item,palette,true);
                DrawSymbol(args.Graphics,symbol,TrailingSymbolBounds(item,args.ToolStrip,14,11),color);
            }
        }

        static Color Ink(ToolStripItem item,ThemePalette palette,bool secondary) {
            if(!item.Enabled)return palette.Muted;
            if(VisualDesign.HighContrast&&(item.Selected||item.Pressed))return palette.SelectedForeground;
            return secondary?palette.Muted:palette.Foreground;
        }

        protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs args) {
            args.TextColor=Ink(args.Item,ThemePalette.Current(Style.Dark),false);
            float scale=Scale(args.ToolStrip);
            int left=(int)Math.Round(12*scale),right=(int)Math.Round(ItemRight(args.Item,args.ToolStrip)-46*scale);
            args.TextRectangle=new Rectangle(left,0,Math.Max(0,right-left),args.Item.Height);
            args.TextFormat=(args.TextFormat&~(TextFormatFlags.Bottom|TextFormatFlags.Right|TextFormatFlags.HorizontalCenter))|
                TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis;
            base.OnRenderItemText(args);
        }

        protected override void OnRenderItemCheck(ToolStripItemImageRenderEventArgs args) {}

        protected override void OnRenderArrow(ToolStripArrowRenderEventArgs args) {
            var menu=args.Item.GetCurrentParent();
            if(menu==null)return;
            var item=args.Item as ToolStripMenuItem;
            bool hasSymbol=item!=null&&(item.CheckState!=CheckState.Unchecked||item.Tag is string);
            DrawSymbol(args.Graphics,args.Direction==ArrowDirection.Left?"back":"chevron",TrailingSymbolBounds(args.Item,menu,12,hasSymbol?29:11),
                Ink(args.Item,ThemePalette.Current(Style.Dark),true));
        }

        static int ItemRight(ToolStripItem item,ToolStrip menu) {
            return Math.Min(item.Width,menu.ClientSize.Width-item.Bounds.Left);
        }

        static RectangleF TrailingSymbolBounds(ToolStripItem item,ToolStrip menu,int glyphSize,int inset) {
            float scale=Scale(menu),size=glyphSize*scale;
            return new RectangleF(ItemRight(item,menu)-inset*scale-size,(item.Height-size)/2,size,size);
        }

        static void DrawSymbol(Graphics graphics,string symbol,RectangleF bounds,Color color) {
            float size=Math.Min(bounds.Width,bounds.Height);
            if(size<=0)return;
            var previous=graphics.SmoothingMode;
            graphics.SmoothingMode=SmoothingMode.AntiAlias;
            VisualDesign.DrawSymbol(graphics,symbol,new RectangleF(bounds.X+(bounds.Width-size)/2,bounds.Y+(bounds.Height-size)/2,size,size),color);
            graphics.SmoothingMode=previous;
        }

        protected override void OnRenderSeparator(ToolStripSeparatorRenderEventArgs args) {
            int inset=(int)Math.Round(12*Scale(args.ToolStrip));
            int y=args.Item.Height/2;
            using(var pen=new Pen(ThemePalette.Current(Style.Dark).Border))args.Graphics.DrawLine(pen,inset,y,args.Item.Width-inset,y);
        }
    }
}
