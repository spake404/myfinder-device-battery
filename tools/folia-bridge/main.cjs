'use strict';
const crypto = require('crypto');
const fs = require('fs');
const http = require('http');
const path = require('path');
const RPC = {
    cmd: 'mf-cmd',
    state: 'mf-state',
    lyrics: 'mf-lyrics',
    queue: 'mf-queue',
    playlists: 'mf-playlists',
    heartbeat: 'mf-heartbeat',
};
function bridgeFilePath() {
    const localAppData = process.env.LOCALAPPDATA || path.join(process.env.USERPROFILE || '', 'AppData', 'Local');
    return path.join(localAppData, 'MyFinderDeviceBattery', 'folia-bridge.json');
}
module.exports = function activate(api) {
    let disposed = false;
    let server = null;
    let lastHeartbeat = Date.now();
    let lastState = null;
    let lastLyrics = { lines: [] };
    let lastQueue = { items: [], total: 0, currentIndex: -1 };
    let lastPlaylists = { items: [], error: '' };
    const token = crypto.randomBytes(24).toString('hex');
    const infoPath = bridgeFilePath();
    const sendJson = (res, status, payload) => {
        const body = JSON.stringify(payload);
        res.writeHead(status, {
            'content-type': 'application/json; charset=utf-8',
            'cache-control': 'no-store',
        });
        res.end(body);
    };
    const writeBridgeFile = (port) => {
        try {
            fs.mkdirSync(path.dirname(infoPath), { recursive: true });
            const tmp = infoPath + '.' + process.pid + '.tmp';
            fs.writeFileSync(tmp, JSON.stringify({
                pid: process.pid,
                port,
                token,
                startedAt: Date.now(),
            }), 'utf8');
            fs.renameSync(tmp, infoPath);
        } catch (err) {
            api.log.warn('写入 folia-bridge.json 失败', err);
        }
    };
    const removeBridgeFile = () => {
        try {
            if (fs.existsSync(infoPath)) {
                const raw = fs.readFileSync(infoPath, 'utf8');
                const parsed = JSON.parse(raw);
                if (parsed && parsed.pid === process.pid) {
                    fs.unlinkSync(infoPath);
                }
            }
        } catch {
        }
    };
    server = http.createServer((req, res) => {
        if (disposed) {
            sendJson(res, 503, { error: 'disposed' });
            return;
        }
        if (req.headers['x-myfinder-token'] !== token) {
            sendJson(res, 403, { error: 'forbidden' });
            return;
        }
        const url = new URL(req.url || '/', 'http://127.0.0.1');
        if (req.method === 'GET' && url.pathname === '/state') {
            const clientAlive = Date.now() - lastHeartbeat < 10000;
            sendJson(res, 200, {
                connected: true,
                clientAlive,
                ...(lastState || {}),
            });
            return;
        }
        if (req.method === 'GET' && url.pathname === '/lyrics') {
            sendJson(res, 200, lastLyrics);
            return;
        }
        if (req.method === 'GET' && url.pathname === '/queue') {
            sendJson(res, 200, lastQueue);
            return;
        }
        if (req.method === 'GET' && url.pathname === '/playlists') {
            sendJson(res, 200, lastPlaylists);
            return;
        }
        if (req.method === 'POST' && url.pathname === '/cmd') {
            let body = '';
            req.on('data', (chunk) => {
                body += chunk;
                if (body.length > 16384) req.destroy();
            });
            req.on('end', () => {
                try {
                    const payload = body ? JSON.parse(body) : {};
                    if (payload && payload.action === 'panel') {
                        try {
                            const { BrowserWindow } = require('electron');
                            const wins = BrowserWindow.getAllWindows().filter((w) => w && !w.isDestroyed());
                            const mainWin = wins[0];
                            if (mainWin) {
                                if (mainWin.isMinimized()) mainWin.restore();
                                mainWin.show();
                                mainWin.focus();
                                sendJson(res, 200, { ok: true });
                                return;
                            }
                        } catch {
                        }
                    }
                    api.rpc.send(RPC.cmd, payload);
                    sendJson(res, 200, { ok: true });
                } catch (err) {
                    sendJson(res, 400, { ok: false, error: String(err?.message ?? err) });
                }
            });
            return;
        }
        sendJson(res, 404, { error: 'not_found' });
    });
    server.listen(0, '127.0.0.1', () => {
        const addr = server.address();
        if (addr && typeof addr === 'object' && addr.port) {
            writeBridgeFile(addr.port);
            api.log.info('MyFinder Folia 桥接已就绪: 127.0.0.1:' + addr.port);
        }
    });
    api.rpc.handle(RPC.heartbeat, () => {
        lastHeartbeat = Date.now();
        return true;
    });
    api.rpc.handle(RPC.state, (snapshot) => {
        lastHeartbeat = Date.now();
        lastState = snapshot ?? null;
        return true;
    });
    api.rpc.handle(RPC.lyrics, (payload) => {
        lastLyrics = payload && Array.isArray(payload.lines) ? payload : { lines: [] };
        return true;
    });
    api.rpc.handle(RPC.queue, (payload) => {
        lastQueue = payload && Array.isArray(payload.items) ? payload : { items: [], total: 0, currentIndex: -1 };
        return true;
    });
    api.rpc.handle(RPC.playlists, (payload) => {
        lastPlaylists = payload && Array.isArray(payload.items)
            ? payload
            : { items: [], error: String(payload?.error || '') };
        return true;
    });
    const dispose = () => {
        if (disposed) return;
        disposed = true;
        removeBridgeFile();
        try {
            if (server) server.close();
        } catch {
        }
        server = null;
    };
    api.onDispose(dispose);
    return dispose;
};
