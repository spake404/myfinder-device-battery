export default function activate(folium) {
    const CH = {
        cmd: 'mf-cmd',
        state: 'mf-state',
        lyrics: 'mf-lyrics',
        queue: 'mf-queue',
        playlists: 'mf-playlists',
        heartbeat: 'mf-heartbeat',
    };
    if (!folium.rpc || typeof folium.rpc.on !== 'function') {
        folium.log.error('宿主 Folium 版本过低，缺少 rpc 能力');
        return;
    }
    let cachedLines = [];
    let lastStateJson = '';
    let lastLyricsJson = '';
    let lastQueueJson = '';
    let lastPlaylistsJson = '';
    const normalizeLines = (rawLines) => {
        if (!Array.isArray(rawLines)) return [];
        return rawLines
            .map((line) => ({
                time: Number(line?.time ?? line?.startTime ?? 0),
                text: String(line?.text ?? line?.content ?? '').trim(),
                subText: String(line?.translatedText ?? line?.translation ?? line?.subText ?? '').trim(),
            }))
            .filter((line) => line.text.length > 0)
            .slice(0, 300);
    };
    const findActiveLyrics = (positionSec) => {
        if (!cachedLines.length) return { currentLyric: '', nextLyric: '' };
        const posMs = Number(positionSec) * 1000;
        const useSeconds = cachedLines.some((l) => l.time > 0 && l.time < 3600) && cachedLines[cachedLines.length - 1].time < 3600;
        const currentVal = useSeconds ? Number(positionSec) : posMs;
        let idx = -1;
        for (let i = 0; i < cachedLines.length; i++) {
            if (cachedLines[i].time <= currentVal + (useSeconds ? 0.15 : 150)) {
                idx = i;
            } else {
                break;
            }
        }
        if (idx < 0) {
            return { currentLyric: cachedLines[0]?.text || '', nextLyric: '' };
        }
        const current = cachedLines[idx];
        const nextText = current.subText || cachedLines[idx + 1]?.text || '';
        return { currentLyric: current.text, nextLyric: nextText };
    };
    const pushLyrics = () => {
        try {
            const raw = typeof folium.lyrics?.getLines === 'function' ? folium.lyrics.getLines() : [];
            cachedLines = normalizeLines(raw);
            const json = JSON.stringify(cachedLines);
            if (json === lastLyricsJson) return;
            lastLyricsJson = json;
            folium.rpc.call(CH.lyrics, { lines: cachedLines }).catch(() => {});
        } catch {
        }
    };
    const pushQueue = () => {
        try {
            if (typeof folium.playback?.getQueue !== 'function') return;
            const page = folium.playback.getQueue({ offset: 0, limit: 50 }) || {};
            const rawItems = Array.isArray(page.items) ? page.items : Array.isArray(page) ? page : [];
            const items = rawItems.slice(0, 50).map((item, idx) => ({
                index: Number.isInteger(item?.index) ? item.index : idx,
                title: String(item?.title ?? item?.name ?? item?.song?.title ?? '未知曲目'),
                artist: String(item?.artist ?? item?.song?.artist ?? ''),
                current: Boolean(item?.current || item?.isCurrent),
            }));
            const payload = {
                items,
                total: Number(page.total ?? items.length),
                currentIndex: Number(page.currentIndex ?? items.findIndex((x) => x.current)),
            };
            const json = JSON.stringify(payload);
            if (json === lastQueueJson) return;
            lastQueueJson = json;
            folium.rpc.call(CH.queue, payload).catch(() => {});
        } catch {
        }
    };
    const pushState = (force = false) => {
        try {
            const state = folium.playback.getState() || {};
            const volume = folium.playback.getVolume() || {};
            const loop = folium.playback.getLoopMode?.() || 'off';
            const position = Number(state.position || 0);
            const { currentLyric, nextLyric } = findActiveLyrics(position);
            const song = state.song || {};
            const providerId = String(song.sourceRef?.providerId || song.providerId || '');
            const songId = String(song.sourceRef?.mediaId || song.id || '');
            const coverUrl = String(song.coverUrl || song.album?.picUrl || song.picUrl || '');
            const payload = {
                state: String(state.state || 'stopped'),
                title: String(state.song?.title || ''),
                artist: String(state.song?.artist || ''),
                album: String(state.song?.album || ''),
                songId,
                providerId,
                coverUrl,
                position,
                duration: Number(state.duration || 0),
                liked: Boolean(state.liked),
                canLike: Boolean(state.canLike),
                volume: Number(volume.volume ?? 1),
                muted: Boolean(volume.muted),
                loop: String(loop),
                currentLyric,
                nextLyric,
                updatedAt: Date.now(),
            };
            const json = JSON.stringify(payload);
            if (!force && json === lastStateJson) return;
            lastStateJson = json;
            folium.rpc.call(CH.state, payload).catch(() => {});
        } catch (err) {
            folium.log.warn('读取 Folia 播放状态失败', err);
        }
    };
    const LOOP_NEXT = { off: 'all', all: 'one', one: 'off' };
    const fetchPlaylists = async () => {
        const rawApi = folium.experimental?.['platform.raw'];
        if (!rawApi || typeof rawApi.request !== 'function') {
            folium.rpc.call(CH.playlists, { items: [], error: '当前 Folia 版本不支持歌单接口' }).catch(() => {});
            return;
        }
        try {
            const res = await rawApi.request('netease', 'user_playlist', { limit: 100, offset: 0 });
            const rawList = Array.isArray(res?.body?.playlist) ? res.body.playlist : [];
            const ownerId = rawList.length > 0 ? (rawList[0]?.userId ?? rawList[0]?.creator?.userId ?? null) : null;
            const items = rawList
                .filter((pl) => pl && (Number(pl.specialType) === 5 || !pl.subscribed || (ownerId != null && (pl.userId === ownerId || pl.creator?.userId === ownerId))))
                .slice(0, 60)
                .map((pl) => ({
                    id: String(pl.id),
                    name: String(pl.name || '未命名歌单'),
                    trackCount: Number(pl.trackCount || 0),
                    liked: Number(pl.specialType) === 5,
                    coverUrl: String(pl.coverImgUrl || ''),
                }));
            const payload = { items, error: '' };
            lastPlaylistsJson = JSON.stringify(payload);
            folium.rpc.call(CH.playlists, payload).catch(() => {});
        } catch (err) {
            const msg = String(err?.code || err?.message || err || '读取歌单失败');
            const friendly = msg.includes('auth-required') ? '请先在 Folia 中登录网易云账号' : msg;
            folium.rpc.call(CH.playlists, { items: [], error: friendly }).catch(() => {});
        }
    };
    const actions = {
        toggle: () => folium.playback.toggle(),
        play: () => folium.playback.play(),
        pause: () => folium.playback.pause(),
        next: () => folium.playback.next(),
        previous: () => folium.playback.previous(),
        like: () => folium.playback.toggleLike(),
        mute: () => {
            const { muted } = folium.playback.getVolume();
            folium.playback.setMuted(!muted);
        },
        loop: () => {
            const current = folium.playback.getLoopMode();
            folium.playback.setLoopMode(LOOP_NEXT[current] ?? 'off');
        },
        shuffle: () => {
            if (typeof folium.playback?.shuffleQueue === 'function') {
                folium.playback.shuffleQueue();
            }
        },
        volume: (value) => {
            const v = Number(value);
            if (Number.isFinite(v)) folium.playback.setVolume(Math.min(1, Math.max(0, v)));
        },
        seek: (value) => {
            const s = Number(value);
            if (Number.isFinite(s)) folium.playback.seek(Math.max(0, s));
        },
        playQueueItem: (value) => {
            const idx = Number(value);
            if (Number.isInteger(idx) && idx >= 0 && typeof folium.playback?.playQueueItem === 'function') {
                return folium.playback.playQueueItem(idx);
            }
        },
        addToPlaylist: async (value) => {
            const rawApi = folium.experimental?.['platform.raw'];
            const playlistId = typeof value === 'object' && value ? String(value.playlistId || '') : String(value || '');
            const isLiked = typeof value === 'object' && value ? Boolean(value.liked) : false;
            if (isLiked) {
                const cur = folium.playback.getState() || {};
                if (!cur.liked) folium.playback.toggleLike();
                return;
            }
            if (!rawApi || typeof rawApi.request !== 'function' || !playlistId) return;
            const state = folium.playback.getState() || {};
            const song = state.song || {};
            const songId = String((typeof value === 'object' && value?.songId) || song.sourceRef?.mediaId || song.id || '');
            if (!songId) return;
            await rawApi.request('netease', 'playlist_tracks', { op: 'add', pid: playlistId, tracks: songId });
            fetchPlaylists();
        },
        panel: () => folium.ui?.openPlayerPanel?.(),
        back: () => folium.ui?.back?.(),
        refreshQueue: () => pushQueue(),
        refreshLyrics: () => pushLyrics(),
        refreshPlaylists: () => fetchPlaylists(),
    };
    folium.rpc.on(CH.cmd, (payload) => {
        const action = payload?.action;
        const handler = actions[action];
        if (!handler) return;
        try {
            handler(payload?.value);
        } catch (err) {
            folium.log.warn(`执行 Folia 指令 ${action} 失败`, err);
        }
        setTimeout(() => {
            pushState(true);
            if (action === 'shuffle' || action === 'playQueueItem') pushQueue();
        }, 120);
    });
    [
        'playback.songChanged',
        'playback.stateChanged',
        'playback.volumeChanged',
        'playback.loopModeChanged',
        'playback.likeChanged',
        'playback.seeked',
    ].forEach((eventName) => {
        try {
            folium.events.on(eventName, () => {
                if (eventName === 'playback.songChanged') {
                    pushLyrics();
                    pushQueue();
                }
                pushState(true);
            });
        } catch {
        }
    });
    try {
        folium.events.on('playback.queueChanged', () => pushQueue());
        folium.events.on('lyrics.loaded', () => {
            pushLyrics();
            pushState(true);
        });
    } catch {
    }
    folium.every(500, () => pushState(false));
    folium.every(2000, () => {
        folium.rpc.call(CH.heartbeat, 1).catch(() => {});
    });
    pushLyrics();
    pushQueue();
    pushState(true);
    fetchPlaylists();
    return () => {
        lastStateJson = '';
        lastLyricsJson = '';
        lastQueueJson = '';
    };
}
