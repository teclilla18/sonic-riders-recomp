// Riders Rebuilt Race Room server (Cloudflare Worker + one Durable Object).
// Public chat with typing, chosen names, emoji and GIPHY GIFs; links are refused here, on the server.
// Race lobbies with an in-game invite code. Everything expires after 30 minutes.
// Secrets (set with `npx wrangler secret put <NAME>`): DEV_KEY (the Game Dev's key), GIPHY_KEY.
// Deploy and maintenance: README.md in this folder.

import { hasLink, cleanText, checkName, nameKey, MAX_TEXT } from './filter.js';

const ORIGINS = [/^https:\/\/teclilla18\.github\.io$/, /^http:\/\/(localhost|127\.0\.0\.1)(:\d+)?$/];
const LIFETIME = 30 * 60 * 1000;
const PUBLIC_KEEP = 100;
const ROOM_KEEP = 60;
const PLATFORMS = ['PC', 'Switch', 'Android'];
const DEV_NAME = 'Game Dev';
const GIF_RATING = 'pg';

function corsHeaders(request) {
  const origin = request.headers.get('Origin') || '';
  const headers = {
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
    'Content-Type': 'application/json; charset=utf-8',
    Vary: 'Origin'
  };
  if (ORIGINS.some(pattern => pattern.test(origin))) {
    headers['Access-Control-Allow-Origin'] = origin;
    headers['Access-Control-Allow-Methods'] = 'GET, POST, OPTIONS';
    headers['Access-Control-Allow-Headers'] = 'Authorization, Content-Type, X-Dev-Key';
    headers['Access-Control-Max-Age'] = '86400';
  }
  return headers;
}

export default {
  async fetch(request, env) {
    if (request.method === 'OPTIONS') return new Response(null, { status: 204, headers: corsHeaders(request) });
    const stub = env.RACE_ROOM.get(env.RACE_ROOM.idFromName('main'));
    const response = await stub.fetch(request);
    const headers = new Headers(response.headers);
    for (const [key, value] of Object.entries(corsHeaders(request))) headers.set(key, value);
    return new Response(response.body, { status: response.status, headers });
  }
};

class HttpError extends Error {
  constructor(status, message) { super(message); this.status = status; }
}
const fail = (status, message) => { throw new HttpError(status, message); };

async function sha256(text) {
  const bytes = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2, '0')).join('');
}

function sameSecret(a, b) {
  if (!a || !b || a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

export class RaceRoom {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
    this.users = {};    // user id -> { name, key, seen, ip, renamed }
    this.messages = []; // public chat
    this.lobbies = {};  // lobby id -> { id, host, guest, platform, listed, code, created, messages }
    this.muted = {};    // user id or 'ip:<hash>' -> time muted
    this.nextId = 1;
    this.hits = new Map();     // rate-limit buckets (memory only)
    this.gifCheck = new Map(); // GIPHY id -> { ok, w, h }
    ctx.blockConcurrencyWhile(async () => {
      const saved = await ctx.storage.get('state');
      if (saved) Object.assign(this, saved);
    });
  }

  save() {
    const { users, messages, lobbies, muted, nextId } = this;
    return this.ctx.storage.put('state', { users, messages, lobbies, muted, nextId });
  }

  limit(key, count, windowMs, message) {
    const now = Date.now();
    const list = (this.hits.get(key) || []).filter(t => now - t < windowMs);
    if (list.length >= count) fail(429, message);
    list.push(now);
    this.hits.set(key, list);
    if (this.hits.size > 5000) this.hits.clear();
  }

  tidy() {
    const now = Date.now();
    this.messages = this.messages.filter(m => now - m.t < LIFETIME).slice(-PUBLIC_KEEP);
    for (const [id, lobby] of Object.entries(this.lobbies)) {
      if (now - lobby.created > LIFETIME) delete this.lobbies[id];
    }
    for (const [id, user] of Object.entries(this.users)) {
      // Riders who never picked a name get it back from their ID, so they are only kept for a day.
      const keep = user.key ? 7 * 24 * 3600 * 1000 : 24 * 3600 * 1000;
      if (now - user.seen > keep) delete this.users[id];
    }
  }

  async fetch(request) {
    try {
      return await this.route(request);
    } catch (error) {
      const status = error instanceof HttpError ? error.status : 500;
      if (status === 500) console.error(error);
      return Response.json({ error: status === 500 ? 'The Race Room had a problem. Try again.' : error.message }, { status });
    }
  }

  async route(request) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, '') || '/';
    const auth = request.headers.get('Authorization') || '';
    const token = auth.startsWith('Bearer ') ? auth.slice(7).trim() : '';
    if (!/^[0-9a-f-]{36}$/i.test(token)) fail(401, 'Missing browser ID. Reload the page.');
    const uid = (await sha256('rider:' + token.toLowerCase())).slice(0, 24);
    const ip = (await sha256('ip:' + (request.headers.get('CF-Connecting-IP') || '?'))).slice(0, 24);
    const dev = sameSecret(request.headers.get('X-Dev-Key') || '', this.env.DEV_KEY || '');
    if (request.headers.get('X-Dev-Key') && !dev) {
      this.limit('devkey:' + ip, 5, 15 * 60 * 1000, 'Too many wrong Game Dev keys. Wait 15 minutes.');
      fail(403, 'That Game Dev key is wrong.');
    }

    this.tidy();
    let user = this.users[uid];
    if (!user) {
      this.limit('new:' + ip, 20, 3600 * 1000, 'Too many new visitors from your connection. Try later.');
      user = this.users[uid] = { name: 'Rider ' + uid.slice(0, 6).toUpperCase(), key: '', seen: 0, ip, renamed: 0 };
    }
    user.seen = Date.now();
    user.ip = ip;
    const me = { uid, ip, dev, user, name: dev ? DEV_NAME : user.name };

    let body = {};
    if (request.method === 'POST') {
      this.limit('post:' + ip, 40, 60 * 1000, 'Slow down a little.');
      try { body = await request.json(); } catch { fail(400, 'Bad request.'); }
      if (!body || typeof body !== 'object') fail(400, 'Bad request.');
    } else if (request.method !== 'GET') fail(405, 'Bad request.');

    const get = request.method === 'GET';
    let match;
    if (get && path === '/state') return Response.json(this.state(me));
    if (get && path === '/gifs') return Response.json(await this.searchGifs(url.searchParams.get('q') || '', ip));
    if (!get && path === '/name') return this.done(this.rename(me, body.name));
    if (!get && path === '/chat') return this.done(await this.post(me, this.messages, PUBLIC_KEEP, body));
    if (!get && path === '/lobbies') return this.done(this.createLobby(me, body));
    if (!get && path === '/admin/delete') return this.done(this.adminDelete(me, body));
    if (!get && path === '/admin/mute') return this.done(this.adminMute(me, body, true));
    if (!get && path === '/admin/unmute-all') return this.done(this.adminMute(me, body, false));
    if ((match = path.match(/^\/rooms\/([0-9a-f-]{36})(\/[a-z]+)?$/))) {
      const lobby = this.lobbies[match[1]];
      if (!lobby) fail(404, 'That lobby has closed or expired.');
      const action = match[2] || '';
      if (get && !action) return Response.json(this.roomView(me, lobby));
      if (!get && action === '/join') return this.done(this.join(me, lobby));
      if (!get && action === '/leave') return this.done(this.leave(me, lobby));
      if (!get && action === '/code') return this.done(this.setCode(me, lobby, body.code));
      if (!get && action === '/chat') {
        if (lobby.host !== uid && lobby.guest !== uid) fail(403, 'Join the lobby to chat in it.');
        return this.done(await this.post(me, lobby.messages, ROOM_KEEP, body));
      }
    }
    fail(404, 'Unknown Race Room request.');
  }

  async done(result) {
    await this.save();
    return Response.json(result ?? { ok: true });
  }

  nameOf(uid) {
    return this.users[uid]?.name || 'Rider';
  }

  publicMessage(m, me) {
    const out = { id: m.id, rider: m.rider, t: m.t };
    if (m.dev) out.dev = true;
    if (m.text) out.text = m.text;
    if (m.gif) out.gif = m.gif;
    if (me.uid === m.uid) out.mine = true;
    return out;
  }

  currentLobby(uid) {
    return Object.values(this.lobbies).find(l => l.host === uid || l.guest === uid)?.id || null;
  }

  state(me) {
    return {
      rider: me.name,
      dev: me.dev,
      gifs: Boolean(this.env.GIPHY_KEY),
      muted: this.isMuted(me),
      currentLobby: this.currentLobby(me.uid),
      lobbies: Object.values(this.lobbies)
        .filter(l => l.listed && !l.guest)
        .sort((a, b) => b.created - a.created)
        .map(l => ({ id: l.id, host: l.hostName, platform: l.platform })),
      messages: this.messages.map(m => this.publicMessage(m, me))
    };
  }

  isMuted(me) {
    return !me.dev && Boolean(this.muted[me.uid] || this.muted['ip:' + me.ip]);
  }

  rename(me, raw) {
    if (me.dev) fail(400, 'You always show as Game Dev while signed in as the Game Dev.');
    if (this.isMuted(me)) fail(403, 'You can’t change your name right now.');
    const checked = checkName(raw);
    if (checked.error) fail(400, checked.error);
    if (Date.now() - me.user.renamed < 20 * 1000) fail(429, 'Wait a few seconds before changing your name again.');
    const now = Date.now();
    const taken = Object.entries(this.users).some(([id, u]) => id !== me.uid && u.key === checked.key && now - u.seen < 24 * 3600 * 1000);
    if (taken) fail(409, 'Someone is already using that name. Try another one.');
    me.user.name = checked.name;
    me.user.key = checked.key;
    me.user.renamed = now;
    for (const lobby of Object.values(this.lobbies)) if (lobby.host === me.uid) lobby.hostName = checked.name;
    return { rider: checked.name };
  }

  async post(me, list, keep, body) {
    if (this.isMuted(me)) fail(403, 'You are muted in the Race Room.');
    const text = cleanText(body.text);
    const gifId = typeof body.gif === 'string' ? body.gif : '';
    if (!text && !gifId) fail(400, 'Type a message first.');
    if ([...text].length > MAX_TEXT) fail(400, 'Messages can be up to ' + MAX_TEXT + ' characters.');
    if (text && !me.dev && hasLink(text)) fail(400, 'Only the Game Dev can send links in the Race Room.');
    if (!me.dev) {
      this.limit('msg:' + me.uid, 1, 1500, 'Slow down a little.');
      this.limit('msg10:' + me.uid, 6, 20 * 1000, 'Slow down a little.');
      this.limit('msgip:' + me.ip, 12, 20 * 1000, 'Slow down a little.');
      if (gifId) this.limit('gif:' + me.uid, 3, 60 * 1000, 'That’s a lot of GIFs. Wait a minute.');
      const last = [...list].reverse().find(m => m.uid === me.uid);
      if (last && text && last.text === text && Date.now() - last.t < 120 * 1000) fail(429, 'You just sent that.');
    }
    let gif;
    if (gifId) gif = await this.checkGif(gifId);
    const message = { id: this.nextId++, uid: me.uid, rider: me.name, t: Date.now() };
    if (me.dev) message.dev = true;
    if (text) message.text = text;
    if (gif) message.gif = gif;
    list.push(message);
    if (list.length > keep) list.splice(0, list.length - keep);
    return { ok: true };
  }

  async giphy(path, params) {
    const key = this.env.GIPHY_KEY;
    if (!key) fail(503, 'GIFs are not set up yet.');
    const url = new URL('https://api.giphy.com/v1/gifs' + path);
    url.searchParams.set('api_key', key);
    for (const [k, v] of Object.entries(params)) url.searchParams.set(k, v);
    const response = await fetch(url, { cf: { cacheTtl: 300 } });
    if (!response.ok) fail(response.status === 404 ? 400 : 502, response.status === 404 ? 'That GIF is not available.' : 'GIPHY did not answer. Try again.');
    return response.json();
  }

  static gifInfo(item) {
    const image = item?.images?.fixed_height;
    if (!item || item.type !== 'gif' || !/^[A-Za-z0-9]{5,40}$/.test(item.id || '') || !image) return null;
    return { id: item.id, w: Number(image.width) || 200, h: Number(image.height) || 200, title: String(item.title || 'GIF').slice(0, 80) };
  }

  async searchGifs(query, ip) {
    this.limit('gifsearch:' + ip, 30, 60 * 1000, 'Too many GIF searches. Wait a minute.');
    const q = cleanText(query).slice(0, 50);
    const data = q
      ? await this.giphy('/search', { q, limit: '24', rating: GIF_RATING, lang: 'en', bundle: 'messaging_non_clips' })
      : await this.giphy('/trending', { limit: '24', rating: GIF_RATING, bundle: 'messaging_non_clips' });
    const gifs = (data.data || []).map(RaceRoom.gifInfo).filter(Boolean);
    for (const gif of gifs) this.gifCheck.set(gif.id, gif);
    return { gifs };
  }

  async checkGif(id) {
    if (!/^[A-Za-z0-9]{5,40}$/.test(id)) fail(400, 'Only GIFs from the GIF picker can be sent.');
    let gif = this.gifCheck.get(id);
    if (!gif) {
      const data = await this.giphy('/' + id, {});
      const item = data.data;
      if (!item || !['g', 'pg'].includes(item.rating)) fail(400, 'That GIF can’t be sent here.');
      gif = RaceRoom.gifInfo(item);
      if (!gif) fail(400, 'Only GIFs from the GIF picker can be sent.');
      this.gifCheck.set(id, gif);
      if (this.gifCheck.size > 3000) this.gifCheck.clear();
    }
    return { id: gif.id, w: gif.w, h: gif.h };
  }

  createLobby(me, body) {
    if (this.isMuted(me)) fail(403, 'You are muted in the Race Room.');
    if (!PLATFORMS.includes(body.platform)) fail(400, 'Choose your platform.');
    if (this.currentLobby(me.uid)) fail(409, 'You are already in a lobby. Leave it first.');
    this.limit('lobby:' + me.ip, 6, 10 * 60 * 1000, 'Too many lobbies. Wait a few minutes.');
    const id = crypto.randomUUID();
    this.lobbies[id] = { id, host: me.uid, hostName: me.name, guest: null, platform: body.platform, listed: body.listed !== false, code: null, created: Date.now(), messages: [] };
    return { id };
  }

  roomView(me, lobby) {
    const participant = lobby.host === me.uid || lobby.guest === me.uid;
    const view = {
      platform: lobby.platform,
      listed: lobby.listed,
      host: lobby.hostName,
      full: Boolean(lobby.guest),
      participant,
      isHost: lobby.host === me.uid,
      guest: null,
      gameCode: null,
      messages: []
    };
    if (participant) {
      view.guest = lobby.guest ? this.nameOf(lobby.guest) : null;
      view.gameCode = lobby.code;
      view.messages = lobby.messages.map(m => this.publicMessage(m, me));
    }
    return view;
  }

  join(me, lobby) {
    if (lobby.host === me.uid || lobby.guest === me.uid) return { ok: true };
    if (lobby.guest) fail(409, 'This lobby is full.');
    if (this.currentLobby(me.uid)) fail(409, 'You are already in a lobby. Leave it first.');
    lobby.guest = me.uid;
    return { ok: true };
  }

  leave(me, lobby) {
    if (lobby.host === me.uid) delete this.lobbies[lobby.id];
    else if (lobby.guest === me.uid) { lobby.guest = null; lobby.code = null; }
    return { ok: true };
  }

  setCode(me, lobby, code) {
    if (lobby.host !== me.uid) fail(403, 'Only the host can share the game code.');
    if (code === null) { lobby.code = null; return { ok: true }; }
    if (!lobby.guest) fail(409, 'Wait for a race partner first.');
    if (typeof code !== 'string' || !/^\d{10}(\d{5})?$/.test(code.trim())) fail(400, 'Game codes are 10 or 15 digits.');
    lobby.code = code.trim();
    return { ok: true };
  }

  findMessage(id) {
    const lists = [this.messages, ...Object.values(this.lobbies).map(l => l.messages)];
    for (const list of lists) {
      const index = list.findIndex(m => m.id === id);
      if (index >= 0) return { list, index, message: list[index] };
    }
    return null;
  }

  adminDelete(me, body) {
    if (!me.dev) fail(403, 'Only the Game Dev can do that.');
    const found = this.findMessage(Number(body.id));
    if (found) found.list.splice(found.index, 1);
    return { ok: true };
  }

  adminMute(me, body, mute) {
    if (!me.dev) fail(403, 'Only the Game Dev can do that.');
    if (!mute) { this.muted = {}; return { ok: true }; }
    const found = this.findMessage(Number(body.id));
    if (!found) fail(404, 'That message is gone.');
    const author = found.message.uid;
    if (author === me.uid || found.message.dev) fail(400, 'You can’t mute yourself.');
    this.muted[author] = Date.now();
    const ip = this.users[author]?.ip;
    if (ip) this.muted['ip:' + ip] = Date.now();
    // Take their messages down too.
    this.messages = this.messages.filter(m => m.uid !== author);
    for (const lobby of Object.values(this.lobbies)) lobby.messages = lobby.messages.filter(m => m.uid !== author);
    return { ok: true };
  }
}
