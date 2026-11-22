using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using AtkBattery;

namespace AtkBatteryTray {
    static class FoliaPillLayout {
        internal const int Width = 408;
        internal const int Height = 148;
        internal const int QueueWidth = 360;
        internal const int QueueHeight = 460;
        internal const int QueueHeaderHeight = 48;
        internal const int QueueStepHeight = 64;
        internal const int PlaylistWidth = 340;
        internal const int PlaylistHeight = 356;
        internal const int PlaylistHeaderHeight = 104;
        internal const int PlaylistStepHeight = 56;
        internal const int PlaylistOverlayOffset = 460;
        static readonly RectangleF PlaylistOverlayRect = new RectangleF(34, 8, 340, 448);
        static readonly RectangleF TopPillRect = new RectangleF(75, 10, 258, 38);
        static readonly RectangleF MainPillRect = new RectangleF(12, 58, 384, 78);
        internal static Size PanelSize { get { return new Size(Width, Height); } }
        internal static Size PanelWithPlaylistsSize { get { return new Size(Width, Height + PlaylistOverlayOffset); } }
        internal static Size QueuePanelSize { get { return new Size(QueueWidth, QueueHeight); } }
        internal static Size PlaylistPanelSize { get { return new Size(PlaylistWidth, PlaylistHeight); } }

        internal static double RatioAt(RectangleF rect, float x) {
            if (rect.Width <= 1) return 0;
            return Math.Max(0, Math.Min(1, (x - rect.X) / rect.Width));
        }

        internal static List<PopupControl> BuildControls(TrayContext owner, PopupNavigation nav, AudioBindings pref) {
            var fs = owner.FoliaState;
            var controls = new List<PopupControl>();
            float yOff = nav.FoliaPlaylistsOpen ? PlaylistOverlayOffset : 0f;
            string layoutKey = nav.FoliaPlaylistsOpen ? "folia:pill:playlists" : "folia:pill";
            var topPill = new RectangleF(TopPillRect.X, TopPillRect.Y + yOff, TopPillRect.Width, TopPillRect.Height);
            var mainPill = new RectangleF(MainPillRect.X, MainPillRect.Y + yOff, MainPillRect.Width, MainPillRect.Height);
            float topX = topPill.X + 12;
            float topY = topPill.Y + 5;
            if (nav.FoliaVolumeOpen) {
                double vol = nav.FoliaVolumePreview >= 0 ? nav.FoliaVolumePreview : (fs.Muted ? 0 : Math.Max(0, Math.Min(1, fs.Volume)));
                int pct = (int)Math.Round(vol * 100);
                controls.Add(new PopupControl { Id = 828, LayoutKey = layoutKey, Text = "收起音量条", Checked = true, Rect = new RectangleF(topX, topY, 28, 28) });
                controls.Add(new PopupControl { Id = 827, Text = "音量", Value = pct + "%", Slider = true, Rect = new RectangleF(topX + 34, topPill.Y + 4, topPill.Right - (topX + 34) - 44, 30) });
            } else {
                controls.Add(new PopupControl { Id = 828, LayoutKey = layoutKey, Text = "调整音量", Value = (int)Math.Round(Math.Max(0, Math.Min(1, fs.Volume)) * 100) + "%", Checked = fs.Muted, Rect = new RectangleF(topX, topY, 28, 28) });
                topX += 28;
                controls.Add(new PopupControl { Id = 823, Text = fs.Liked ? "取消喜欢" : "喜欢此歌曲", Checked = fs.Liked, Enabled = fs.CanLike, Rect = new RectangleF(topX, topY, 28, 28) });
                topX += 28;
                controls.Add(new PopupControl { Id = 833, Text = "收藏到歌单", Checked = nav.FoliaPlaylistsOpen, Rect = new RectangleF(topX, topY, 28, 28) });
                topX += 28;
                controls.Add(new PopupControl { Id = 825, Text = "随机打乱队列", Rect = new RectangleF(topX, topY, 28, 28) });
                topX += 28;
                controls.Add(new PopupControl { Id = 831, Text = "打开播放器主界面", Rect = new RectangleF(topX, topY, 28, 28) });
                topX += 28;
                controls.Add(new PopupControl { Id = 832, Text = "显示歌词", Checked = pref.FoliaLyricsEnabled, Value = pref.FoliaLyricsEnabled ? "已开启" : "已关闭", Rect = new RectangleF(topX, topY, 28, 28) });
                topX += 28;
                controls.Add(new PopupControl { Id = 811, Text = "Folia 任务栏样式", Value = FoliaModes.Name(pref.FoliaTaskbarMode), Rect = new RectangleF(topX, topY, 28, 28) });
                topX += 28 + 10; // divider gap
                controls.Add(new PopupControl { Id = 6, Text = "返回主面板", Rect = new RectangleF(topX, topY, 28, 28) });
            }

            // Bottom main pill controls
            controls.Add(new PopupControl { Id = 820, Text = fs.IsPlaying ? "暂停" : "播放", Rect = new RectangleF(mainPill.X + 16, mainPill.Y + 15, 48, 48) });
            controls.Add(new PopupControl { Id = 821, Text = "上一首", Rect = new RectangleF(mainPill.X + 78, mainPill.Y + 12, 26, 26) });
            controls.Add(new PopupControl { Id = 822, Text = "下一首", Rect = new RectangleF(mainPill.X + 256, mainPill.Y + 12, 26, 26) });
            controls.Add(new PopupControl { Id = 826, Text = "拖动调整播放进度", Slider = true, Enabled = fs.Duration > 0, Rect = new RectangleF(mainPill.X + 118, mainPill.Y + 43, 124, 20) });
            controls.Add(new PopupControl { Id = 824, Text = "循环模式", Value = fs.LoopLabel, Rect = new RectangleF(mainPill.X + 294, mainPill.Y + 22, 34, 34) });
            controls.Add(new PopupControl { Id = 830, Text = "播放队列", Value = owner.FoliaQueueCount > 0 ? owner.FoliaQueueCount + " 首" : "点击查看", Rect = new RectangleF(mainPill.X + 334, mainPill.Y + 22, 34, 34) });
            if (nav.FoliaPlaylistsOpen) {
                AppendPlaylistControls(controls, owner, nav, PlaylistOverlayRect, 834);
            }
            return controls;
        }

        internal static void Paint(Graphics g, TrayContext owner, PopupNavigation nav, List<PopupControl> controls, bool dark, int hover, int pressed, int focus) {
            var fs = owner.FoliaState;
            if (fs == null) return;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;

            Color pillBg = dark ? Color.FromArgb(248, 38, 38, 42) : Color.FromArgb(252, 255, 255, 255);
            Color pillBorder = dark ? Color.FromArgb(65, 255, 255, 255) : Color.FromArgb(28, 0, 0, 0);
            Color fg = dark ? Color.FromArgb(242, 242, 245) : Color.FromArgb(24, 24, 27);
            Color muted = dark ? Color.FromArgb(165, 165, 172) : Color.FromArgb(115, 115, 122);
            Color subtle = dark ? Color.FromArgb(46, 255, 255, 255) : Color.FromArgb(24, 0, 0, 0);
            Color hoverBg = dark ? Color.FromArgb(28, 255, 255, 255) : Color.FromArgb(16, 0, 0, 0);
            Color pressBg = dark ? Color.FromArgb(42, 255, 255, 255) : Color.FromArgb(28, 0, 0, 0);
            Color accent = dark ? Color.FromArgb(96, 205, 255) : Color.FromArgb(0, 120, 212);
            Color heartRed = Color.FromArgb(229, 72, 77);
            Color trackBg = dark ? Color.FromArgb(64, 64, 70) : Color.FromArgb(232, 232, 236);
            Color trackFill = dark ? Color.FromArgb(242, 242, 245) : Color.FromArgb(24, 24, 27);

            float yOff = nav.FoliaPlaylistsOpen ? PlaylistOverlayOffset : 0f;
            var topPill = new RectangleF(TopPillRect.X, TopPillRect.Y + yOff, TopPillRect.Width, TopPillRect.Height);
            var mainPill = new RectangleF(MainPillRect.X, MainPillRect.Y + yOff, MainPillRect.Width, MainPillRect.Height);
            DrawPillSurface(g, topPill, 19f, pillBg, pillBorder, dark);
            DrawPillSurface(g, mainPill, 39f, pillBg, pillBorder, dark);

            if (!nav.FoliaVolumeOpen) {
                float divX = topPill.X + 12 + 7 * 28 + 4.5f;
                using (var divPen = new Pen(subtle, 1f))
                    g.DrawLine(divPen, divX, topPill.Y + 11, divX, topPill.Bottom - 11);
            }

            foreach (var c in controls) {
                if (c.Id == 834 || c.Id >= 2000) continue;
                bool isHover = hover == c.Id || focus == c.Id;
                bool isPress = pressed == c.Id;
                if (c.Id == 826 || c.Id == 827) continue;
                if (c.Id == 820) {
                    Color btnBg = dark
                        ? (isPress ? Color.FromArgb(215, 215, 220) : isHover ? Color.FromArgb(255, 255, 255) : Color.FromArgb(242, 242, 245))
                        : (isPress ? Color.FromArgb(48, 48, 52) : isHover ? Color.FromArgb(34, 34, 38) : Color.FromArgb(20, 20, 22));
                    Color btnFg = dark ? Color.FromArgb(20, 20, 22) : Color.White;
                    using (var b = new SolidBrush(btnBg)) g.FillEllipse(b, c.Rect);
                    DrawPlayPauseGlyph(g, c.Rect, fs.IsPlaying, btnFg);
                    continue;
                }
                if (c.Id == 824) {
                    bool activeLoop = fs.Loop != "none" && !String.IsNullOrEmpty(fs.Loop);
                    if (activeLoop || isHover || isPress) {
                        Color loopBg = isPress ? pressBg : activeLoop ? (dark ? Color.FromArgb(56, 56, 62) : Color.FromArgb(235, 235, 239)) : hoverBg;
                        using (var b = new SolidBrush(loopBg)) g.FillEllipse(b, c.Rect);
                    }
                    DrawLoopGlyph(g, c.Rect, fs.Loop, (activeLoop || isHover) ? fg : muted);
                    continue;
                }
                if (isHover || isPress) {
                    using (var b = new SolidBrush(isPress ? pressBg : hoverBg))
                        g.FillEllipse(b, c.Rect);
                }
                Color iconColor = !c.Enabled ? Color.FromArgb(95, muted) : (isHover ? fg : muted);
                float animT = -1f;
                if (nav.FoliaAnimActionId == c.Id && nav.FoliaAnimTick != 0) {
                    int elapsed = unchecked(Environment.TickCount - nav.FoliaAnimTick);
                    if (elapsed >= 0 && elapsed < 300) animT = elapsed / 300f;
                }
                GraphicsState animState = null;
                if (animT >= 0f) {
                    float cx = c.Rect.X + c.Rect.Width / 2f;
                    float cy = c.Rect.Y + c.Rect.Height / 2f;
                    float pulse = (float)Math.Sin(animT * Math.PI);
                    float scale = 1f + (c.Id == 823 ? 0.30f : 0.24f) * pulse;
                    float dx = c.Id == 825 ? (float)Math.Sin(animT * Math.PI * 3f) * 2.2f * (1f - animT) : 0f;
                    float dy = c.Id == 831 ? -2.2f * pulse : 0f;
                    float angle = c.Id == 811 ? 18f * pulse : (c.Id == 6 ? -22f * pulse : 0f);
                    animState = g.Save();
                    g.TranslateTransform(cx + dx, cy + dy);
                    if (angle != 0f) g.RotateTransform(angle);
                    g.ScaleTransform(scale, scale);
                    g.TranslateTransform(-cx, -cy);
                    iconColor = c.Id == 823 ? heartRed : accent;
                }
                switch (c.Id) {
                    case 828: DrawSpeakerGlyph(g, c.Rect, fs.Muted || (nav.FoliaVolumeOpen && (nav.FoliaVolumePreview >= 0 ? nav.FoliaVolumePreview : fs.Volume) <= 0.005), (nav.FoliaVolumeOpen || fs.Muted || animT >= 0f) ? accent : iconColor); break;
                    case 823: DrawHeartGlyph(g, c.Rect, fs.Liked, fs.Liked ? heartRed : iconColor); break;
                    case 833: DrawStarGlyph(g, c.Rect, (nav.FoliaPlaylistsOpen || animT >= 0f) ? accent : iconColor); break;
                    case 825: DrawShuffleGlyph(g, c.Rect, iconColor); break;
                    case 831: DrawWindowGlyph(g, c.Rect, iconColor); break;
                    case 832: DrawLyricsGlyph(g, c.Rect, c.Checked, (c.Checked || animT >= 0f) ? accent : iconColor); break;
                    case 811: DrawGridGlyph(g, c.Rect, iconColor); break;
                    case 6: DrawHomeBackGlyph(g, c.Rect, iconColor); break;
                    case 821: DrawChevronGlyph(g, c.Rect, true, iconColor); break;
                    case 822: DrawChevronGlyph(g, c.Rect, false, iconColor); break;
                    case 830: DrawQueueGlyph(g, c.Rect, iconColor); break;
                }
                if (animState != null) g.Restore(animState);
            }

            if (nav.FoliaVolumeOpen) {
                var volCtrl = controls.Find(c => c.Id == 827);
                if (volCtrl != null) {
                    double vol = nav.FoliaVolumePreview >= 0 ? nav.FoliaVolumePreview : (fs.Muted ? 0 : Math.Max(0, Math.Min(1, fs.Volume)));
                    float vBarX = volCtrl.Rect.X + 4;
                    float vBarW = volCtrl.Rect.Width - 8;
                    float vBarY = volCtrl.Rect.Y + volCtrl.Rect.Height / 2f - 2f;
                    using (var vp = VisualDesign.RoundRect(vBarX, vBarY, vBarW, 4f, 2f))
                    using (var vb = new SolidBrush(trackBg))
                        g.FillPath(vb, vp);
                    float vFillW = (float)(vBarW * vol);
                    if (vFillW >= 2f) {
                        using (var fp = VisualDesign.RoundRect(vBarX, vBarY, vFillW, 4f, 2f))
                        using (var fb = new SolidBrush(trackFill))
                            g.FillPath(fb, fp);
                    }
                    float knobX = vBarX + vFillW;
                    float knobY = vBarY + 2f;
                    using (var kb = new SolidBrush(trackFill))
                        g.FillEllipse(kb, knobX - 5f, knobY - 5f, 10f, 10f);
                    int pct = (int)Math.Round(vol * 100);
                    using (var pf = new Font("Consolas", 10.5f, FontStyle.Regular, GraphicsUnit.Pixel))
                    using (var pb = new SolidBrush(muted))
                    using (var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                        g.DrawString(pct + "%", pf, pb, new RectangleF(volCtrl.Rect.Right + 2, topPill.Y, 34, topPill.Height), sf);
                }
            }

            // Title centered between prev (<) and next (>)
            string title = String.IsNullOrEmpty(fs.Title) ? "等待播放" : fs.Title;
            var titleRect = new RectangleF(mainPill.X + 106, mainPill.Y + 12, 148, 26);
            using (var titleFont = new Font("Microsoft YaHei UI", 12.5f, FontStyle.Bold, GraphicsUnit.Pixel))
            using (var titleBrush = new SolidBrush(fg))
            using (var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center, Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap }) {
                g.DrawString(title, titleFont, titleBrush, titleRect, sf);
            }

            // Progress row: 07:01 [====----] 07:56
            double ratio = nav.FoliaSeekPreview >= 0 ? nav.FoliaSeekPreview : (fs.Duration > 0 ? Math.Max(0, Math.Min(1, fs.Position / fs.Duration)) : 0);
            double displayPos = nav.FoliaSeekPreview >= 0 ? nav.FoliaSeekPreview * fs.Duration : fs.Position;
            string curTime = FormatTwoDigitTime(displayPos);
            string totTime = FormatTwoDigitTime(fs.Duration);
            var timeLeftRect = new RectangleF(mainPill.X + 76, mainPill.Y + 45, 38, 16);
            var timeRightRect = new RectangleF(mainPill.X + 246, mainPill.Y + 45, 38, 16);
            using (var timeFont = new Font("Consolas", 10.5f, FontStyle.Regular, GraphicsUnit.Pixel))
            using (var timeBrush = new SolidBrush(muted))
            using (var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center }) {
                g.DrawString(curTime, timeFont, timeBrush, timeLeftRect, sf);
                g.DrawString(totTime, timeFont, timeBrush, timeRightRect, sf);
            }

            float barX = mainPill.X + 118;
            float barW = 124;
            float barY = mainPill.Y + 51;
            float barH = 4f;
            using (var trackPath = VisualDesign.RoundRect(barX, barY, barW, barH, 2f))
            using (var tb = new SolidBrush(trackBg))
                g.FillPath(tb, trackPath);
            float fillW = (float)(barW * ratio);
            if (fillW >= 2f) {
                using (var fillPath = VisualDesign.RoundRect(barX, barY, fillW, barH, 2f))
                using (var fb = new SolidBrush(trackFill))
                    g.FillPath(fb, fillPath);
            }
            if (hover == 826 || pressed == 826 || nav.FoliaSeekPreview >= 0) {
                float kx = barX + fillW;
                float ky = barY + barH / 2f;
                using (var kb = new SolidBrush(trackFill))
                    g.FillEllipse(kb, kx - 5f, ky - 5f, 10f, 10f);
            }

            if (nav.FoliaPlaylistsOpen) {
                DrawPillSurface(g, PlaylistOverlayRect, 16f, pillBg, pillBorder, dark);
                PaintPlaylistsContent(g, owner, controls, PlaylistOverlayRect, 834, dark, hover, pressed, focus);
            }
        }

        internal static List<PopupControl> BuildQueueControls(TrayContext owner, PopupNavigation nav) {
            var controls = new List<PopupControl>();
            controls.Add(new PopupControl { Id = 6, LayoutKey = "folia:queue", Text = "返回", Rect = new RectangleF(QueueWidth - 42, 10, 30, 30) });
            var queue = owner.FoliaQueue;
            var items = queue != null && queue.Items != null ? queue.Items : new FoliaQueueItem[0];
            int contentH = Math.Max(QueueHeight - QueueHeaderHeight - 16, items.Length * QueueStepHeight + 16);
            int viewH = QueueHeight - QueueHeaderHeight - 16;
            int maxScroll = Math.Max(0, contentH - viewH);
            nav.Offset = Math.Max(0, Math.Min(nav.Offset, maxScroll));
            var viewport = new RectangleF(12, QueueHeaderHeight, QueueWidth - 24, viewH);
            float centerX = QueueWidth / 2f;
            for (int i = 0; i < items.Length; i++) {
                var item = items[i];
                bool onRight = (i % 2) == 0;
                float cardW = item.Current ? 140f : 126f;
                float cardH = item.Current ? 52f : 46f;
                float y = QueueHeaderHeight + 12 + i * QueueStepHeight - nav.Offset + (item.Current ? 0f : 3f);
                float x = onRight ? (centerX + 18f) : (centerX - 18f - cardW);
                controls.Add(new PopupControl {
                    Id = 850 + item.Index,
                    Text = String.IsNullOrEmpty(item.Title) ? "未知曲目" : item.Title,
                    Detail = item.Artist,
                    Selected = item.Current,
                    Viewport = viewport,
                    Rect = new RectangleF(x, y, cardW, cardH)
                });
            }
            return controls;
        }

        internal static void PaintQueue(Graphics g, TrayContext owner, PopupNavigation nav, List<PopupControl> controls, bool dark, int hover, int pressed, int focus) {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;

            var cardRect = new RectangleF(0.5f, 0.5f, QueueWidth - 1f, QueueHeight - 1f);
            Color bg = dark ? Color.FromArgb(72, 28, 28, 32) : Color.FromArgb(65, 240, 240, 244);
            Color border = dark ? Color.FromArgb(72, 255, 255, 255) : Color.FromArgb(85, 255, 255, 255);
            Color fg = dark ? Color.FromArgb(242, 242, 245) : Color.FromArgb(32, 32, 36);
            Color muted = dark ? Color.FromArgb(160, 160, 168) : Color.FromArgb(115, 115, 122);
            Color lineCol = dark ? Color.FromArgb(72, 255, 255, 255) : Color.FromArgb(60, 0, 0, 0);
            Color itemBg = dark ? Color.FromArgb(165, 52, 52, 58) : Color.FromArgb(185, 250, 250, 252);
            Color itemHover = dark ? Color.FromArgb(215, 64, 64, 72) : Color.FromArgb(225, 255, 255, 255);
            Color currentBg = dark ? Color.FromArgb(230, 68, 68, 76) : Color.FromArgb(238, 255, 255, 255);
            Color orangeDot = Color.FromArgb(255, 85, 0);
            Color normalDot = dark ? Color.FromArgb(170, 170, 178) : Color.FromArgb(72, 72, 76);

            using (var path = VisualDesign.RoundRect(cardRect.X, cardRect.Y, cardRect.Width, cardRect.Height, 8f)) {
                using (var b = new SolidBrush(bg)) g.FillPath(b, path);
                using (var p = new Pen(border, 1f)) g.DrawPath(p, path);
            }

            using (var titleFont = new Font("Microsoft YaHei UI", 15f, FontStyle.Bold, GraphicsUnit.Pixel))
            using (var tb = new SolidBrush(fg))
                g.DrawString("播放队列", titleFont, tb, 22, 16);

            var closeCtrl = controls.Find(c => c.Id == 6);
            if (closeCtrl != null) {
                bool closeHover = hover == 6 || pressed == 6 || focus == 6;
                if (closeHover) {
                    using (var hb = new SolidBrush(dark ? Color.FromArgb(28, 255, 255, 255) : Color.FromArgb(18, 0, 0, 0)))
                        g.FillEllipse(hb, closeCtrl.Rect);
                }
                float cx = closeCtrl.Rect.X + closeCtrl.Rect.Width / 2f;
                float cy = closeCtrl.Rect.Y + closeCtrl.Rect.Height / 2f;
                using (var xp = new Pen(muted, 1.6f) { StartCap = LineCap.Round, EndCap = LineCap.Round }) {
                    g.DrawLine(xp, cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f);
                    g.DrawLine(xp, cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f);
                }
            }

            int viewH = QueueHeight - QueueHeaderHeight - 16;
            var clipRect = new RectangleF(12, QueueHeaderHeight, QueueWidth - 24, viewH);
            var saved = g.Save();
            g.SetClip(clipRect, CombineMode.Intersect);

            float centerX = QueueWidth / 2f;
            using (var lp = new Pen(lineCol, 1f))
                g.DrawLine(lp, centerX, QueueHeaderHeight + 4, centerX, QueueHeight - 14);

            bool hasItems = false;
            foreach (var c in controls) {
                if (c.Id < 850) continue;
                hasItems = true;
                if (c.Rect.Bottom < clipRect.Top || c.Rect.Top > clipRect.Bottom) continue;
                bool isHover = hover == c.Id || focus == c.Id;
                bool isCurrent = c.Selected;
                Color cardFill = isCurrent ? currentBg : (isHover ? itemHover : itemBg);
                float dotY = c.Rect.Y + c.Rect.Height / 2f;
                float dotR = isCurrent ? 5.5f : 3.5f;
                using (var db = new SolidBrush(isCurrent ? orangeDot : normalDot))
                    g.FillEllipse(db, centerX - dotR, dotY - dotR, dotR * 2f, dotR * 2f);

                if (isCurrent) {
                    var sr = new RectangleF(c.Rect.X, c.Rect.Y + 2f, c.Rect.Width, c.Rect.Height);
                    using (var sp = VisualDesign.RoundRect(sr.X, sr.Y, sr.Width, sr.Height, 8f))
                    using (var sb = new SolidBrush(Color.FromArgb(dark ? 35 : 16, 0, 0, 0)))
                        g.FillPath(sb, sp);
                }
                using (var cp = VisualDesign.RoundRect(c.Rect.X, c.Rect.Y, c.Rect.Width, c.Rect.Height, 8f)) {
                    using (var cb = new SolidBrush(cardFill)) g.FillPath(cb, cp);
                    if (isHover || isCurrent) {
                        using (var pen = new Pen(dark ? Color.FromArgb(45, 255, 255, 255) : Color.FromArgb(20, 0, 0, 0), 1f))
                            g.DrawPath(pen, cp);
                    }
                }

                float titleSz = isCurrent ? 12f : 11f;
                float artistSz = isCurrent ? 10f : 9.5f;
                using (var tf = new Font("Microsoft YaHei UI", titleSz, isCurrent ? FontStyle.Bold : FontStyle.Regular, GraphicsUnit.Pixel))
                using (var af = new Font("Microsoft YaHei UI", artistSz, FontStyle.Regular, GraphicsUnit.Pixel))
                using (var tb = new SolidBrush(isCurrent ? fg : Color.FromArgb(210, fg)))
                using (var ab = new SolidBrush(muted))
                using (var sf = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap }) {
                    float textX = c.Rect.X + 10f;
                    float textW = c.Rect.Width - 20f;
                    float topPad = String.IsNullOrEmpty(c.Detail) ? (c.Rect.Height - 16f) / 2f : (isCurrent ? 9f : 7f);
                    g.DrawString(c.Text, tf, tb, new RectangleF(textX, c.Rect.Y + topPad, textW, 18f), sf);
                    if (!String.IsNullOrEmpty(c.Detail))
                        g.DrawString(c.Detail, af, ab, new RectangleF(textX, c.Rect.Y + topPad + (isCurrent ? 18f : 16f), textW, 15f), sf);
                }
            }
            if (!hasItems) {
                using (var ef = new Font("Microsoft YaHei UI", 12.5f, FontStyle.Regular, GraphicsUnit.Pixel))
                using (var eb = new SolidBrush(muted))
                using (var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                    g.DrawString("当前播放队列为空", ef, eb, clipRect, sf);
            }
            g.Restore(saved);
        }

        internal static List<PopupControl> BuildPlaylistsControls(TrayContext owner, PopupNavigation nav) {
            var controls = new List<PopupControl>();
            var card = new RectangleF(0, 0, PlaylistWidth, PlaylistHeight);
            AppendPlaylistControls(controls, owner, nav, card, 6, "folia:playlists");
            return controls;
        }

        static void AppendPlaylistControls(List<PopupControl> controls, TrayContext owner, PopupNavigation nav, RectangleF card, int closeId, string layoutKey = null) {
            controls.Add(new PopupControl { Id = closeId, LayoutKey = layoutKey, Text = "返回", Rect = new RectangleF(card.Right - 42, card.Y + 10, 30, 30) });
            var snap = owner.FoliaPlaylists;
            var items = snap != null && snap.Items != null ? snap.Items : new FoliaPlaylistItem[0];
            var fs = owner.FoliaState;
            bool likedSong = fs != null && fs.Liked;
            int viewH = (int)card.Height - PlaylistHeaderHeight - 14;
            int contentH = Math.Max(viewH, items.Length * PlaylistStepHeight + 6);
            int maxScroll = Math.Max(0, contentH - viewH);
            nav.Offset = Math.Max(0, Math.Min(nav.Offset, maxScroll));
            var viewport = new RectangleF(card.X + 12, card.Y + PlaylistHeaderHeight, card.Width - 24, viewH);
            for (int i = 0; i < items.Length && i < 500; i++) {
                var pl = items[i];
                float y = card.Y + PlaylistHeaderHeight + 4 + i * PlaylistStepHeight - nav.Offset;
                controls.Add(new PopupControl {
                    Id = 2000 + i,
                    Text = String.IsNullOrEmpty(pl.Name) ? "未命名歌单" : pl.Name,
                    Detail = pl.Liked ? (likedSong ? "已喜欢" : "我喜欢的音乐") : (pl.TrackCount + " 首歌曲"),
                    ImageUrl = pl.Liked ? null : pl.CoverUrl,
                    Selected = pl.Liked,
                    Checked = pl.Liked && likedSong,
                    Viewport = viewport,
                    Rect = new RectangleF(card.X + 16, y, card.Width - 32, 50)
                });
            }
        }

        internal static void PaintPlaylists(Graphics g, TrayContext owner, PopupNavigation nav, List<PopupControl> controls, bool dark, int hover, int pressed, int focus) {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;

            var cardRect = new RectangleF(0.5f, 0.5f, PlaylistWidth - 1f, PlaylistHeight - 1f);
            Color bg = dark ? Color.FromArgb(72, 28, 28, 32) : Color.FromArgb(65, 240, 240, 244);
            Color border = dark ? Color.FromArgb(72, 255, 255, 255) : Color.FromArgb(85, 255, 255, 255);
            using (var path = VisualDesign.RoundRect(cardRect.X, cardRect.Y, cardRect.Width, cardRect.Height, 8f)) {
                using (var b = new SolidBrush(bg)) g.FillPath(b, path);
                using (var p = new Pen(border, 1f)) g.DrawPath(p, path);
            }
            PaintPlaylistsContent(g, owner, controls, new RectangleF(0, 0, PlaylistWidth, PlaylistHeight), 6, dark, hover, pressed, focus);
        }

        static void PaintPlaylistsContent(Graphics g, TrayContext owner, List<PopupControl> controls, RectangleF card, int closeId, bool dark, int hover, int pressed, int focus) {
            Color fg = dark ? Color.FromArgb(242, 242, 245) : Color.FromArgb(32, 32, 36);
            Color muted = dark ? Color.FromArgb(160, 160, 168) : Color.FromArgb(115, 115, 122);
            Color itemBg = dark ? Color.FromArgb(135, 52, 52, 58) : Color.FromArgb(165, 244, 244, 248);
            Color itemHover = dark ? Color.FromArgb(200, 66, 66, 74) : Color.FromArgb(235, 255, 255, 255);
            Color accent = Color.FromArgb(255, 85, 0);

            using (var titleFont = new Font("Microsoft YaHei UI", 16.5f, FontStyle.Bold, GraphicsUnit.Pixel))
            using (var tb = new SolidBrush(fg))
                g.DrawString("收藏到歌单", titleFont, tb, card.X + 18, card.Y + 15);

            var closeCtrl = controls.Find(c => c.Id == closeId);
            if (closeCtrl != null) {
                bool closeHover = hover == closeId || pressed == closeId || focus == closeId;
                if (closeHover) {
                    using (var hb = new SolidBrush(dark ? Color.FromArgb(28, 255, 255, 255) : Color.FromArgb(18, 0, 0, 0)))
                        g.FillEllipse(hb, closeCtrl.Rect);
                }
                float cx = closeCtrl.Rect.X + closeCtrl.Rect.Width / 2f;
                float cy = closeCtrl.Rect.Y + closeCtrl.Rect.Height / 2f;
                using (var xp = new Pen(muted, 1.6f) { StartCap = LineCap.Round, EndCap = LineCap.Round }) {
                    g.DrawLine(xp, cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f);
                    g.DrawLine(xp, cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f);
                }
            }

            var fs = owner.FoliaState;
            string songTitle = fs != null && !String.IsNullOrEmpty(fs.Title) ? fs.Title : "未在播放";
            string songArtist = fs != null && !String.IsNullOrEmpty(fs.Artist) ? fs.Artist : "";
            Image songCover = fs != null && !String.IsNullOrEmpty(fs.CoverUrl) ? owner.GetPlaylistCover(fs.CoverUrl) : null;
            var songBox = new RectangleF(card.X + 16, card.Y + 46, card.Width - 32, 46);
            using (var sp = VisualDesign.RoundRect(songBox.X, songBox.Y, songBox.Width, songBox.Height, 8f))
            using (var sb = new SolidBrush(dark ? Color.FromArgb(110, 48, 48, 54) : Color.FromArgb(180, 245, 245, 248)))
                g.FillPath(sb, sp);
            if (songCover != null) {
                var coverRect = new RectangleF(songBox.X + 6f, songBox.Y + 6f, 34f, 34f);
                using (var cp = VisualDesign.RoundRect(coverRect.X, coverRect.Y, coverRect.Width, coverRect.Height, 6f)) {
                    var imgSaved = g.Save();
                    g.SetClip(cp, CombineMode.Intersect);
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.DrawImage(songCover, coverRect);
                    g.Restore(imgSaved);
                }
            }
            using (var sf = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap })
            using (var stf = new Font("Microsoft YaHei UI", 14f, FontStyle.Bold, GraphicsUnit.Pixel))
            using (var saf = new Font("Microsoft YaHei UI", 12f, FontStyle.Regular, GraphicsUnit.Pixel))
            using (var tb = new SolidBrush(fg))
            using (var ab = new SolidBrush(muted)) {
                float tx = songCover != null ? (songBox.X + 48f) : (songBox.X + 12f);
                float tw = songBox.Right - 12f - tx;
                if (String.IsNullOrEmpty(songArtist)) {
                    g.DrawString(songTitle, stf, tb, new RectangleF(tx, songBox.Y + 13f, tw, 20f), sf);
                } else {
                    g.DrawString(songTitle, stf, tb, new RectangleF(tx, songBox.Y + 5f, tw, 19f), sf);
                    g.DrawString(songArtist, saf, ab, new RectangleF(tx, songBox.Y + 25f, tw, 17f), sf);
                }
            }

            float viewH = card.Height - PlaylistHeaderHeight - 14;
            var clipRect = new RectangleF(card.X + 12, card.Y + PlaylistHeaderHeight, card.Width - 24, viewH);
            var saved = g.Save();
            g.SetClip(clipRect, CombineMode.Intersect);

            bool hasItems = false;
            foreach (var c in controls) {
                if (c.Id < 2000) continue;
                hasItems = true;
                if (c.Rect.Bottom < clipRect.Top || c.Rect.Top > clipRect.Bottom) continue;
                bool isHover = hover == c.Id || focus == c.Id;
                bool isPress = pressed == c.Id;
                using (var cp = VisualDesign.RoundRect(c.Rect.X, c.Rect.Y, c.Rect.Width, c.Rect.Height, 8f)) {
                    using (var cb = new SolidBrush(isPress || isHover ? itemHover : itemBg)) g.FillPath(cb, cp);
                    if (isHover) {
                        using (var pen = new Pen(dark ? Color.FromArgb(45, 255, 255, 255) : Color.FromArgb(20, 0, 0, 0), 1f))
                            g.DrawPath(pen, cp);
                    }
                }
                var badge = new RectangleF(c.Rect.X + 8f, c.Rect.Y + 8f, 34f, 34f);
                using (var bp = VisualDesign.RoundRect(badge.X, badge.Y, badge.Width, badge.Height, 7f))
                using (var bb = new SolidBrush(c.Selected ? Color.FromArgb(38, accent.R, accent.G, accent.B) : (dark ? Color.FromArgb(75, 255, 255, 255) : Color.FromArgb(18, 0, 0, 0))))
                    g.FillPath(bb, bp);
                if (c.Selected) {
                    DrawHeartGlyph(g, badge, true, accent);
                } else {
                    Image cover = !String.IsNullOrEmpty(c.ImageUrl) ? owner.GetPlaylistCover(c.ImageUrl) : null;
                    if (cover != null) {
                        using (var bp = VisualDesign.RoundRect(badge.X, badge.Y, badge.Width, badge.Height, 7f)) {
                            var imgSaved = g.Save();
                            g.SetClip(bp, CombineMode.Intersect);
                            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                            g.DrawImage(cover, badge);
                            g.Restore(imgSaved);
                        }
                    } else {
                        DrawQueueGlyph(g, badge, muted);
                    }
                }

                float rightPad = c.Checked ? 86f : 14f;
                float textX = badge.Right + 10f;
                float textW = Math.Max(40f, c.Rect.Right - rightPad - textX);
                using (var tf = new Font("Microsoft YaHei UI", 14f, c.Selected ? FontStyle.Bold : FontStyle.Regular, GraphicsUnit.Pixel))
                using (var df = new Font("Microsoft YaHei UI", 12f, FontStyle.Regular, GraphicsUnit.Pixel))
                using (var tb = new SolidBrush(fg))
                using (var db = new SolidBrush(muted))
                using (var sf = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap }) {
                    if (c.Selected) {
                        g.DrawString(c.Text, tf, tb, new RectangleF(textX, c.Rect.Y + 15f, textW, 20f), sf);
                    } else {
                        g.DrawString(c.Text, tf, tb, new RectangleF(textX, c.Rect.Y + 6f, textW, 20f), sf);
                        g.DrawString(c.Detail, df, db, new RectangleF(textX, c.Rect.Y + 27f, textW, 17f), sf);
                    }
                    if (c.Checked) {
                        using (var rsf = new StringFormat { Alignment = StringAlignment.Far, LineAlignment = StringAlignment.Center })
                            g.DrawString("✓ 已经喜欢了", df, db, new RectangleF(c.Rect.Right - 90f, c.Rect.Y, 80f, c.Rect.Height), rsf);
                    }
                }
            }
            if (!hasItems) {
                var snap = owner.FoliaPlaylists;
                string emptyMsg = snap == null ? "正在加载歌单…" : (!String.IsNullOrEmpty(snap.Error) ? snap.Error : "暂无可用歌单");
                using (var ef = new Font("Microsoft YaHei UI", 14f, FontStyle.Regular, GraphicsUnit.Pixel))
                using (var eb = new SolidBrush(muted))
                using (var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                    g.DrawString(emptyMsg, ef, eb, clipRect, sf);
            }
            g.Restore(saved);
        }

        static string FormatTwoDigitTime(double seconds) {
            if (Double.IsNaN(seconds) || Double.IsInfinity(seconds) || seconds < 0) seconds = 0;
            int total = (int)Math.Floor(seconds);
            return (total / 60).ToString("D2") + ":" + (total % 60).ToString("D2");
        }

        static void DrawPillSurface(Graphics g, RectangleF rect, float radius, Color fill, Color border, bool dark) {
            int[] alphas = dark ? new[] { 24, 16, 8 } : new[] { 12, 8, 4 };
            for (int i = 2; i >= 0; i--) {
                float inflate = (i + 1) * 2.2f;
                var sr = new RectangleF(rect.X - inflate * 0.45f, rect.Y + (i + 1) * 1.4f, rect.Width + inflate * 0.9f, rect.Height + inflate * 0.7f);
                using (var sp = VisualDesign.RoundRect(sr.X, sr.Y, sr.Width, sr.Height, radius + inflate * 0.5f))
                using (var sb = new SolidBrush(Color.FromArgb(alphas[i], 0, 0, 0)))
                    g.FillPath(sb, sp);
            }
            using (var path = VisualDesign.RoundRect(rect.X, rect.Y, rect.Width, rect.Height, radius)) {
                using (var b = new SolidBrush(fill)) g.FillPath(b, path);
                using (var p = new Pen(border, 1f)) g.DrawPath(p, path);
            }
        }

        static void DrawPlayPauseGlyph(Graphics g, RectangleF rect, bool playing, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var b = new SolidBrush(color)) {
                if (playing) {
                    using (var left = VisualDesign.RoundRect(cx - 5.5f, cy - 7f, 4f, 14f, 1.5f))
                    using (var right = VisualDesign.RoundRect(cx + 1.5f, cy - 7f, 4f, 14f, 1.5f)) {
                        g.FillPath(b, left);
                        g.FillPath(b, right);
                    }
                } else {
                    var pts = new[] {
                        new PointF(cx - 4.5f, cy - 7.5f),
                        new PointF(cx + 7.5f, cy),
                        new PointF(cx - 4.5f, cy + 7.5f)
                    };
                    using (var pen = new Pen(color, 2f) { LineJoin = LineJoin.Round }) {
                        g.FillPolygon(b, pts);
                        g.DrawPolygon(pen, pts);
                    }
                }
            }
        }

        static void DrawChevronGlyph(Graphics g, RectangleF rect, bool left, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            float dx = left ? 2.5f : -2.5f;
            using (var pen = new Pen(color, 1.8f) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round }) {
                g.DrawLines(pen, new[] {
                    new PointF(cx + dx, cy - 4.5f),
                    new PointF(cx - dx, cy),
                    new PointF(cx + dx, cy + 4.5f)
                });
            }
        }

        static void DrawSpeakerGlyph(Graphics g, RectangleF rect, bool muted, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var pen = new Pen(color, 1.4f) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round }) {
                g.DrawPolygon(pen, new[] {
                    new PointF(cx - 6f, cy - 2.2f),
                    new PointF(cx - 3.2f, cy - 2.2f),
                    new PointF(cx + 0.5f, cy - 5.2f),
                    new PointF(cx + 0.5f, cy + 5.2f),
                    new PointF(cx - 3.2f, cy + 2.2f),
                    new PointF(cx - 6f, cy + 2.2f)
                });
                if (muted) {
                    g.DrawLine(pen, cx + 3f, cy - 2.5f, cx + 6.5f, cy + 2.5f);
                    g.DrawLine(pen, cx + 6.5f, cy - 2.5f, cx + 3f, cy + 2.5f);
                } else {
                    g.DrawArc(pen, cx - 1.5f, cy - 3.2f, 5f, 6.4f, -55, 110);
                    g.DrawArc(pen, cx - 1.5f, cy - 5.2f, 8f, 10.4f, -55, 110);
                }
            }
        }

        static void DrawHeartGlyph(Graphics g, RectangleF rect, bool liked, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f + 0.5f;
            using (var path = new GraphicsPath()) {
                path.AddBezier(cx, cy + 5.5f, cx - 7.5f, cy + 0.5f, cx - 7f, cy - 6f, cx - 3f, cy - 6f);
                path.AddBezier(cx - 3f, cy - 6f, cx - 1f, cy - 6f, cx, cy - 4f, cx, cy - 3.5f);
                path.AddBezier(cx, cy - 3.5f, cx, cy - 4f, cx + 1f, cy - 6f, cx + 3f, cy - 6f);
                path.AddBezier(cx + 3f, cy - 6f, cx + 7f, cy - 6f, cx + 7.5f, cy + 0.5f, cx, cy + 5.5f);
                path.CloseFigure();
                if (liked) {
                    using (var b = new SolidBrush(color)) g.FillPath(b, path);
                } else {
                    using (var pen = new Pen(color, 1.4f) { LineJoin = LineJoin.Round }) g.DrawPath(pen, path);
                }
            }
        }

        static void DrawStarGlyph(Graphics g, RectangleF rect, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f + 0.5f;
            float outerR = 6.3f;
            float innerR = 2.75f;
            var pts = new PointF[10];
            for (int i = 0; i < 10; i++) {
                double angle = -Math.PI / 2.0 + i * Math.PI / 5.0;
                float r = (i % 2 == 0) ? outerR : innerR;
                pts[i] = new PointF(cx + (float)(Math.Cos(angle) * r), cy + (float)(Math.Sin(angle) * r));
            }
            using (var pen = new Pen(color, 1.4f) { LineJoin = LineJoin.Round })
                g.DrawPolygon(pen, pts);
        }

        static void DrawShuffleGlyph(Graphics g, RectangleF rect, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var pen = new Pen(color, 1.4f) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round }) {
                g.DrawLines(pen, new[] { new PointF(cx - 6f, cy - 3.5f), new PointF(cx - 2f, cy - 3.5f), new PointF(cx + 2f, cy + 3.5f), new PointF(cx + 6f, cy + 3.5f) });
                g.DrawLines(pen, new[] { new PointF(cx - 6f, cy + 3.5f), new PointF(cx - 2f, cy + 3.5f), new PointF(cx + 2f, cy - 3.5f), new PointF(cx + 6f, cy - 3.5f) });
                g.DrawLines(pen, new[] { new PointF(cx + 4f, cy - 5.2f), new PointF(cx + 6f, cy - 3.5f), new PointF(cx + 4f, cy - 1.8f) });
                g.DrawLines(pen, new[] { new PointF(cx + 4f, cy + 1.8f), new PointF(cx + 6f, cy + 3.5f), new PointF(cx + 4f, cy + 5.2f) });
            }
        }

        static void DrawWindowGlyph(Graphics g, RectangleF rect, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var path = VisualDesign.RoundRect(cx - 6f, cy - 4.5f, 12f, 9f, 2f))
            using (var pen = new Pen(color, 1.4f)) {
                g.DrawPath(pen, path);
                g.DrawLine(pen, cx - 6f, cy - 1.5f, cx + 6f, cy - 1.5f);
            }
        }

        static void DrawLyricsGlyph(Graphics g, RectangleF rect, bool enabled, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var pen = new Pen(color, 1.4f) { StartCap = LineCap.Round, EndCap = LineCap.Round }) {
                g.DrawLine(pen, cx - 5.5f, cy - 4f, cx + 5.5f, cy - 4f);
                g.DrawLine(pen, cx - 5.5f, cy, cx + 2f, cy);
                g.DrawLine(pen, cx - 5.5f, cy + 4f, cx + 0.5f, cy + 4f);
                g.DrawLine(pen, cx + 4.5f, cy - 0.5f, cx + 4.5f, cy + 4.2f);
                g.DrawLine(pen, cx + 4.5f, cy - 0.5f, cx + 6.5f, cy + 0.5f);
            }
            using (var b = new SolidBrush(color))
                g.FillEllipse(b, cx + 2.3f, cy + 2.8f, 3f, 2.6f);
        }

        static void DrawGridGlyph(Graphics g, RectangleF rect, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var pen = new Pen(color, 1.35f)) {
                for (int r = 0; r < 2; r++) {
                    for (int c = 0; c < 2; c++) {
                        float x = cx - 5.5f + c * 6.5f;
                        float y = cy - 5.5f + r * 6.5f;
                        using (var p = VisualDesign.RoundRect(x, y, 4.5f, 4.5f, 1.2f))
                            g.DrawPath(pen, p);
                    }
                }
            }
        }

        static void DrawHomeBackGlyph(Graphics g, RectangleF rect, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var pen = new Pen(color, 1.35f)) {
                g.DrawRectangle(pen, cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);
                g.DrawEllipse(pen, cx - 5.2f, cy - 5.2f, 3f, 3f);
                g.DrawEllipse(pen, cx + 2.2f, cy - 5.2f, 3f, 3f);
                g.DrawEllipse(pen, cx - 5.2f, cy + 2.2f, 3f, 3f);
                g.DrawEllipse(pen, cx + 2.2f, cy + 2.2f, 3f, 3f);
            }
        }

        static void DrawLoopGlyph(Graphics g, RectangleF rect, string loopMode, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var pen = new Pen(color, 1.5f) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round }) {
                g.DrawLines(pen, new[] { new PointF(cx - 6f, cy - 1f), new PointF(cx - 6f, cy - 4f), new PointF(cx + 5f, cy - 4f) });
                g.DrawLines(pen, new[] { new PointF(cx + 3f, cy - 6f), new PointF(cx + 5.5f, cy - 4f), new PointF(cx + 3f, cy - 2f) });
                g.DrawLines(pen, new[] { new PointF(cx + 6f, cy + 1f), new PointF(cx + 6f, cy + 4f), new PointF(cx - 5f, cy + 4f) });
                g.DrawLines(pen, new[] { new PointF(cx - 3f, cy + 2f), new PointF(cx - 5.5f, cy + 4f), new PointF(cx - 3f, cy + 6f) });
            }
            if (loopMode == "one") {
                using (var f = new Font("Segoe UI", 7.5f, FontStyle.Bold, GraphicsUnit.Pixel))
                using (var b = new SolidBrush(color))
                using (var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                    g.DrawString("1", f, b, new RectangleF(cx - 4f, cy - 4f, 8f, 8f), sf);
            }
        }

        static void DrawQueueGlyph(Graphics g, RectangleF rect, Color color) {
            float cx = rect.X + rect.Width / 2f;
            float cy = rect.Y + rect.Height / 2f;
            using (var pen = new Pen(color, 1.4f) { StartCap = LineCap.Round, EndCap = LineCap.Round }) {
                g.DrawLine(pen, cx - 5.5f, cy - 4f, cx - 5.5f, cy + 4f);
                g.DrawLine(pen, cx - 5.5f, cy + 4f, cx + 5.5f, cy + 4f);
                g.DrawLine(pen, cx - 1.5f, cy - 4f, cx + 5.5f, cy - 4f);
                g.DrawLine(pen, cx - 1.5f, cy - 1.2f, cx + 5.5f, cy - 1.2f);
                g.DrawLine(pen, cx - 1.5f, cy + 1.5f, cx + 3.5f, cy + 1.5f);
            }
        }
    }
}
