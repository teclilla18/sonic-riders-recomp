(() => {
  'use strict';
  const byId = id => document.getElementById(id);
  const app = byId('race-room');
  const board = byId('race-board');
  const lobby = byId('race-lobby');
  const status = byId('race-status');
  const create = byId('race-create');
  const codeForm = byId('race-code-form');
  const resume = byId('race-resume');
  const tokenKey = 'riders-plus-race-identity';
  const devKeyName = 'riders-race-dev-key';
  const lobbyPattern = /^#[0-9a-f-]{36}$/;
  const MAX_TEXT = 240;
  const EMOJI = ['😀', '😂', '🤣', '😅', '😎', '🤩', '🥳', '😮', '😢', '😡', '🤔', '🙏', '👍', '👎', '👏', '🙌', '💪', '🤝',
    '👋', '✌️', '🔥', '⚡', '💨', '🏁', '🏆', '🥇', '🥈', '🥉', '🎮', '🕹️', '💍', '⭐', '🌀', '💙', '💛', '❤️', '💯', '✅',
    '❌', '⏱️', '🚀', '🌪️', '🦔', '🦅', '🐺', '🐱', '🤖', '👀', 'GG'];
  let api_base = app.dataset.api;
  try {
    const override = new URLSearchParams(location.search).get('api');
    if (override && /^(localhost|127\.0\.0\.1)$/.test(location.hostname)) api_base = override;
  } catch {}

  let identity;
  let devKey = '';
  try {
    identity = localStorage.getItem(tokenKey);
    if (!identity) {
      identity = crypto.randomUUID();
      localStorage.setItem(tokenKey, identity);
    }
    devKey = localStorage.getItem(devKeyName) || '';
  } catch {
    identity ||= crypto.randomUUID();
  }

  let timer;
  let refreshing = false;
  let refreshAgain = false;
  let pending = false;
  let connectionFailed = false;
  let rider = '';
  let isDev = false;
  let gifsReady = false;
  let currentLobby = null;
  let activeRoom = null;
  let lastActivity = Date.now();
  const logKeys = new WeakMap();

  // Same idea as the server's filter (server/race-room/filter.js); here only to grey out Send early.
  function hasLink(text) {
    const view = String(text).normalize('NFKC').toLowerCase()
      .replace(/[。｡․‧·∙⋅・]/g, '.')
      .replace(/\p{Cf}/gu, '')
      .replace(/\s*(?:\[\s*\.\s*\]|\(\s*\.\s*\)|\{\s*\.\s*\}|\[\s*dot\s*\]|\(\s*dot\s*\)|\{\s*dot\s*\}|<\s*dot\s*>)\s*/g, '.')
      .replace(/(\S)\s+dot\s+(?=\S)/g, '$1.');
    return /[a-z][a-z0-9+.-]{1,15}:\/\/|(?:^|[^a-z0-9])www\d?\.|\b\d{1,3}(?:\.\d{1,3}){3}\b|\b(?:discord(?:app)?|dsc)\s*\.?\s*gg\b/.test(view) ||
      /(?:^|[^\p{L}\p{N}_-])[\p{L}\p{N}-]{1,63}(?:\.[\p{L}\p{N}-]{1,63})*\.(?:com|net|org|gg|io|ly|me|co|xyz|tv|app|dev|info|biz|us|uk|ru|de|fr|br|link|site|online|store|shop|live|fun|club|top|click|to|be|sh|ai|cc|ws|tk|gl|lol|page|pro|win|vip|games|chat|news|blog|email|zip|mov|art|fm|im|tw|su|tech|space|website|cloud|stream)(?![\p{L}\p{N}])/u.test(view);
  }

  function showStatus(text, error = false) {
    if (status.textContent !== text) status.textContent = text;
    status.toggleAttribute('data-error', error);
  }

  async function api(path, data) {
    const options = {
      headers: { Authorization: 'Bearer ' + identity },
      credentials: 'omit',
      referrerPolicy: 'no-referrer',
      signal: AbortSignal.timeout(9000)
    };
    if (devKey) options.headers['X-Dev-Key'] = devKey;
    if (data !== undefined) {
      options.method = 'POST';
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(data);
    }
    const response = await fetch(api_base + path, options);
    const result = await response.json();
    if (!response.ok) throw Object.assign(new Error(result.error), { status: response.status });
    return result;
  }

  function roomId() {
    return lobbyPattern.test(location.hash) ? location.hash.slice(1) : '';
  }

  function openRoom(id) {
    if (id) history.pushState(null, '', '#' + id);
    else history.pushState(null, '', location.pathname + location.search);
    resetRoom();
    refresh();
  }

  function resetRoom() {
    activeRoom = null;
    byId('race-join').hidden = true;
    byId('race-members').hidden = true;
    byId('race-invite-link').hidden = true;
    byId('race-code-value').textContent = '';
    const log = byId('race-lobby-log');
    logKeys.delete(log);
    log.replaceChildren();
    codeForm.reset();
    byId('race-lobby-meta').textContent = '';
  }

  function gifUrl(id) {
    return /^[A-Za-z0-9]{5,40}$/.test(id) ? 'https://media.giphy.com/media/' + id + '/200.gif' : '';
  }

  function modButton(label, path, id) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'race-mod';
    button.textContent = label;
    button.addEventListener('click', () => action(button, () => api(path, { id })));
    return button;
  }

  function drawMessages(list, messages) {
    const key = (isDev ? 'dev|' : '') + messages.map(message => message.id).join('|');
    if (logKeys.get(list) === key) return;
    logKeys.set(list, key);
    const nearEnd = list.scrollHeight - list.clientHeight - list.scrollTop < 80;
    // Signing in or out as the Game Dev adds or removes the Delete / Mute buttons: draw every row again.
    const mode = isDev ? 'dev' : 'rider';
    if (list.dataset.mode !== mode) {
      list.dataset.mode = mode;
      list.replaceChildren();
    }
    const previous = new Map(Array.from(list.children).map(item => [item.dataset.key, item]));
    const keep = new Set();
    for (const message of messages) {
      const id = String(message.id);
      keep.add(id);
      if (previous.has(id)) continue;
      const row = document.createElement('li');
      row.dataset.key = id;
      if (message.dev) row.classList.add('is-dev');
      const author = document.createElement('strong');
      author.className = 'race-author';
      author.textContent = message.rider;
      row.append(author);
      if (message.text) {
        const text = document.createElement('span');
        text.className = 'race-text';
        text.textContent = message.text;
        row.append(text);
      }
      if (message.gif && gifUrl(message.gif.id)) {
        const image = document.createElement('img');
        image.className = 'race-gif';
        image.src = gifUrl(message.gif.id);
        image.alt = 'GIF';
        image.loading = 'lazy';
        image.referrerPolicy = 'no-referrer';
        image.width = Math.round(message.gif.w * (120 / message.gif.h)) || 160;
        image.height = 120;
        image.addEventListener('load', () => { if (nearEnd) list.scrollTop = list.scrollHeight; }, { once: true });
        row.append(image);
      }
      if (isDev && !message.dev) {
        const tools = document.createElement('span');
        tools.className = 'race-mod-tools';
        tools.append(modButton('Delete', '/admin/delete', message.id), modButton('Mute', '/admin/mute', message.id));
        row.append(tools);
      }
      list.append(row);
    }
    for (const [id, row] of previous) {
      if (!keep.has(id)) row.remove();
    }
    if (!messages.length) {
      const row = document.createElement('li');
      row.dataset.key = 'empty';
      row.className = 'race-empty';
      row.textContent = 'Quiet in here. Say hi!';
      list.append(row);
    } else {
      list.querySelector('.race-empty')?.remove();
    }
    if (messages.length && messages[messages.length - 1].t > Date.now() - 120000) lastActivity = Date.now();
    if (nearEnd) list.scrollTop = list.scrollHeight;
  }

  function drawLobbies(lobbies) {
    const list = byId('race-lobbies');
    const found = new Map(Array.from(list.children).map(item => [item.dataset.lobbyId, item]));
    for (const { id, host, platform } of lobbies) {
      if (found.delete(id)) continue;
      const row = document.createElement('li');
      row.dataset.lobbyId = id;
      const label = document.createElement('span');
      label.textContent = host + ' · ' + platform;
      const link = document.createElement('a');
      link.href = '#' + id;
      link.textContent = 'View lobby';
      link.addEventListener('click', event => {
        if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
        event.preventDefault();
        openRoom(id);
      });
      row.append(label, link);
      list.append(row);
    }
    for (const row of found.values()) row.remove();
    if (!lobbies.length) {
      const row = document.createElement('li');
      row.textContent = 'No open public lobbies. Create one to find a partner.';
      list.append(row);
    }
  }

  function drawRoom(id, info) {
    const changed = activeRoom?.id !== id;
    activeRoom = { id, ...info };
    byId('race-lobby-meta').textContent = info.participant
      ? [info.platform, info.listed ? 'Public listing' : 'Invite only', info.guest ? info.host + ' and ' + info.guest : 'Waiting for a race partner'].join(' · ')
      : info.platform + (info.full ? ' · This lobby is full' : ' · One spot open');
    byId('race-join').hidden = info.participant || info.full;
    byId('race-members').hidden = !info.participant;
    board.hidden = true;
    lobby.hidden = false;
    if (changed) byId('race-lobby-title').focus();
    if (!info.participant) return;
    byId('race-leave').querySelector('.btn-in').textContent = info.isHost ? 'Close lobby' : 'Leave lobby';
    codeForm.hidden = !info.isHost || !info.guest;
    byId('race-code-clear').hidden = !info.isHost || !info.gameCode;
    byId('race-code-value').textContent = info.gameCode ? 'Game code: ' + info.gameCode : 'No game invite shared yet.';
    drawMessages(byId('race-lobby-log'), info.messages);
  }

  function drawMe(data) {
    rider = data.rider;
    isDev = Boolean(data.dev);
    gifsReady = Boolean(data.gifs);
    const name = byId('race-my-name');
    name.textContent = rider;
    name.classList.toggle('is-dev', isDev);
    byId('race-name-edit').hidden = isDev;
    for (const button of document.querySelectorAll('.race-gif-toggle')) button.hidden = !gifsReady;
    byId('race-muted').hidden = !data.muted;
    byId('race-dev-tools').hidden = !isDev;
  }

  function schedule() {
    clearTimeout(timer);
    // Every 4 s while people are chatting, every 8 s when the room is quiet (keeps the free server plan happy).
    const delay = Date.now() - lastActivity < 3 * 60 * 1000 ? 4000 : 8000;
    if (!document.hidden) timer = setTimeout(refresh, delay);
  }

  async function refresh() {
    if (refreshing) {
      refreshAgain = true;
      return;
    }
    refreshing = true;
    const id = roomId();
    try {
      if (!rider || !id) {
        const data = await api('/state');
        if (roomId() !== id) return;
        drawMe(data);
        currentLobby = data.currentLobby;
        drawMessages(byId('race-public-log'), data.messages);
        drawLobbies(data.lobbies);
        resume.hidden = !currentLobby;
        create.hidden = Boolean(currentLobby);
      }
      if (id) {
        const info = await api('/rooms/' + id);
        if (roomId() !== id) return;
        drawRoom(id, info);
      } else {
        lobby.hidden = true;
        board.hidden = false;
      }
      if (connectionFailed || !status.hasAttribute('data-error')) showStatus('Connected as ' + rider);
      connectionFailed = false;
    } catch (error) {
      if (roomId() !== id) return;
      if (error.status === 403 && devKey && /Game Dev key/.test(error.message)) {
        setDevKey('');
        showStatus(error.message, true);
        refreshAgain = true;
      } else if (id && error.status === 404) {
        history.replaceState(null, '', location.pathname + location.search);
        resetRoom();
        showStatus(error.message, true);
        refreshAgain = true;
      } else {
        connectionFailed = true;
        showStatus(error.status ? error.message : 'Connection interrupted. Reconnecting…', true);
      }
    } finally {
      refreshing = false;
      if (refreshAgain) {
        refreshAgain = false;
        refresh();
      } else schedule();
    }
  }

  async function action(button, task) {
    if (pending) return false;
    pending = true;
    if (button) button.disabled = true;
    let ok = false;
    try {
      await task();
      ok = true;
      lastActivity = Date.now();
      showStatus('Connected as ' + rider);
      await refresh();
    } catch (error) {
      showStatus(error.status ? error.message : 'Could not reach the Race Room. Try again.', true);
    } finally {
      if (button) button.disabled = false;
      pending = false;
    }
    return ok;
  }

  // One chat box (public or lobby): text, emoji, GIFs. `path` gives the server path for sending.
  function setupComposer(form, path) {
    const input = form.querySelector('.race-input');
    const send = form.querySelector('.race-send');
    const warn = form.querySelector('.race-link-warn');
    const count = form.querySelector('.race-count');
    const emojiBox = form.querySelector('.race-emoji');
    const gifBox = form.querySelector('.race-gifs');
    input.maxLength = MAX_TEXT * 2;

    function update() {
      const length = [...input.value].length;
      const link = hasLink(input.value);
      warn.hidden = !link;
      count.textContent = length > MAX_TEXT - 40 ? length + ' / ' + MAX_TEXT : '';
      count.toggleAttribute('data-over', length > MAX_TEXT);
      send.disabled = link || !input.value.trim() || length > MAX_TEXT;
    }
    input.addEventListener('input', update);
    update();

    form.addEventListener('submit', async event => {
      event.preventDefault();
      if (send.disabled) return;
      const text = input.value;
      if (await action(send, () => api(path(), { text }))) {
        input.value = '';
        closePickers();
        update();
        input.focus();
      } else update();
    });

    function closePickers(except) {
      if (except !== emojiBox) emojiBox.hidden = true;
      if (except !== gifBox) gifBox.hidden = true;
    }

    for (const emoji of EMOJI) {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = emoji;
      button.setAttribute('aria-label', 'Add ' + emoji);
      button.addEventListener('click', () => {
        const start = input.selectionStart ?? input.value.length;
        const end = input.selectionEnd ?? input.value.length;
        const insert = emoji === 'GG' ? 'GG ' : emoji;
        input.setRangeText(insert, start, end, 'end');
        update();
        input.focus();
      });
      emojiBox.querySelector('.race-emoji-grid').append(button);
    }
    form.querySelector('.race-emoji-toggle').addEventListener('click', () => {
      closePickers(emojiBox);
      emojiBox.hidden = !emojiBox.hidden;
    });

    const gifSearch = gifBox.querySelector('input');
    const gifGrid = gifBox.querySelector('.race-gif-grid');
    const gifNote = gifBox.querySelector('.race-gif-note');
    let gifTimer;
    let gifQuery = null;
    async function loadGifs() {
      const q = gifSearch.value.trim();
      if (q === gifQuery) return;
      gifQuery = q;
      gifNote.textContent = 'Loading GIFs…';
      try {
        const { gifs } = await api('/gifs?q=' + encodeURIComponent(q));
        if (gifQuery !== q) return;
        gifGrid.replaceChildren();
        for (const gif of gifs) {
          const button = document.createElement('button');
          button.type = 'button';
          button.title = gif.title || 'GIF';
          const image = document.createElement('img');
          image.src = gifUrl(gif.id);
          image.alt = gif.title || 'GIF';
          image.loading = 'lazy';
          image.referrerPolicy = 'no-referrer';
          button.append(image);
          button.addEventListener('click', async () => {
            if (await action(button, () => api(path(), { gif: gif.id }))) gifBox.hidden = true;
          });
          gifGrid.append(button);
        }
        gifNote.textContent = gifs.length ? '' : 'No GIFs found. Try another word.';
      } catch (error) {
        gifQuery = null;
        gifNote.textContent = error.message || 'GIFs did not load. Try again.';
      }
    }
    gifSearch.addEventListener('input', () => {
      clearTimeout(gifTimer);
      gifTimer = setTimeout(loadGifs, 450);
    });
    gifSearch.addEventListener('keydown', event => {
      if (event.key === 'Enter') { event.preventDefault(); clearTimeout(gifTimer); loadGifs(); }
    });
    form.querySelector('.race-gif-toggle').addEventListener('click', () => {
      closePickers(gifBox);
      gifBox.hidden = !gifBox.hidden;
      if (!gifBox.hidden) { loadGifs(); gifSearch.focus(); }
    });
    form.addEventListener('keydown', event => {
      if (event.key === 'Escape') closePickers();
    });
  }

  // Name
  const nameForm = byId('race-name-form');
  byId('race-name-edit').addEventListener('click', () => {
    nameForm.hidden = !nameForm.hidden;
    if (!nameForm.hidden) {
      nameForm.elements.name.value = rider.startsWith('Rider ') ? '' : rider;
      nameForm.elements.name.focus();
    }
  });
  nameForm.addEventListener('submit', event => {
    event.preventDefault();
    const name = nameForm.elements.name.value;
    action(event.submitter, async () => {
      const result = await api('/name', { name });
      rider = result.rider;
      byId('race-my-name').textContent = rider;
      nameForm.hidden = true;
    });
  });
  byId('race-name-cancel').addEventListener('click', () => { nameForm.hidden = true; });

  // Tabs: Race Room / Rules
  const tabs = Array.from(document.querySelectorAll('.race-tabs [role="tab"]'));
  function showTab(tab) {
    for (const other of tabs) {
      const selected = other === tab;
      other.setAttribute('aria-selected', String(selected));
      other.tabIndex = selected ? 0 : -1;
      byId(other.getAttribute('aria-controls')).hidden = !selected;
    }
  }
  for (const tab of tabs) {
    tab.addEventListener('click', () => showTab(tab));
    tab.addEventListener('keydown', event => {
      const step = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
      if (!step) return;
      const next = tabs[(tabs.indexOf(tab) + step + tabs.length) % tabs.length];
      next.focus();
      showTab(next);
    });
  }
  for (const link of document.querySelectorAll('[data-open-rules]')) {
    link.addEventListener('click', event => {
      event.preventDefault();
      showTab(byId('race-tab-rules'));
      byId('race-tab-rules').focus();
    });
  }

  // Game Dev sign-in: open race-room.html#dev once on your own browser, paste the key.
  function setDevKey(value) {
    devKey = value;
    try {
      if (value) localStorage.setItem(devKeyName, value);
      else localStorage.removeItem(devKeyName);
    } catch {}
    rider = '';
  }
  const devForm = byId('race-dev-form');
  function checkDevHash() {
    if (location.hash === '#dev') {
      devForm.hidden = false;
      history.replaceState(null, '', location.pathname + location.search);
    }
  }
  devForm.addEventListener('submit', event => {
    event.preventDefault();
    setDevKey(devForm.elements.key.value.trim());
    devForm.reset();
    devForm.hidden = true;
    refresh();
  });
  byId('race-dev-signout').addEventListener('click', () => {
    setDevKey('');
    refresh();
  });
  byId('race-dev-unmute').addEventListener('click', event => {
    action(event.currentTarget, () => api('/admin/unmute-all', {}));
  });

  create.addEventListener('submit', event => {
    event.preventDefault();
    const fields = new FormData(create);
    action(event.submitter, async () => {
      const result = await api('/lobbies', { platform: fields.get('platform'), listed: fields.get('visibility') === 'public' });
      openRoom(result.id);
    });
  });
  codeForm.addEventListener('submit', event => {
    event.preventDefault();
    const code = new FormData(codeForm).get('code').trim();
    action(event.submitter, async () => {
      await api('/rooms/' + roomId() + '/code', { code });
      codeForm.reset();
    });
  });
  byId('race-code-clear').addEventListener('click', event => {
    action(event.currentTarget, () => api('/rooms/' + roomId() + '/code', { code: null }));
  });
  byId('race-join').addEventListener('click', event => {
    action(event.currentTarget, () => api('/rooms/' + roomId() + '/join', {}));
  });
  byId('race-leave').addEventListener('click', event => {
    action(event.currentTarget, async () => {
      await api('/rooms/' + roomId() + '/leave', {});
      openRoom(null);
    });
  });
  byId('race-copy').addEventListener('click', async () => {
    const link = new URL(location.pathname + '#' + roomId(), location.origin).href;
    try {
      await navigator.clipboard.writeText(link);
      showStatus('Lobby link copied.');
    } catch {
      const field = byId('race-invite-url');
      field.value = link;
      byId('race-invite-link').hidden = false;
      field.select();
    }
  });
  byId('race-back').addEventListener('click', () => openRoom(null));
  resume.addEventListener('click', () => {
    if (currentLobby) openRoom(currentLobby);
  });
  setupComposer(byId('race-public-form'), () => '/chat');
  setupComposer(byId('race-lobby-form'), () => '/rooms/' + roomId() + '/chat');
  window.addEventListener('popstate', () => {
    checkDevHash();
    resetRoom();
    refresh();
  });
  window.addEventListener('hashchange', checkDevHash);
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) clearTimeout(timer);
    else refresh();
  });
  checkDevHash();
  refresh();
})();
