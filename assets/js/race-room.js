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
  const lobbyPattern = /^#[0-9a-f-]{36}$/;
  const publicPhrases = ['Looking for a race!', 'I can host', 'Can you host?', 'What platform are you on?', 'Same beta build?', 'Good race!'];
  const roomPhrases = ['Ready to join', 'Give me a minute', 'Same beta build?', 'Check the game invite code', 'Connection failed', 'Rematch?', 'Good race!', 'Thanks for playing!'];
  let identity;
  try {
    identity = localStorage.getItem(tokenKey);
    if (!identity) {
      identity = crypto.randomUUID();
      localStorage.setItem(tokenKey, identity);
    }
  } catch {
    identity ||= crypto.randomUUID();
  }

  let timer;
  let refreshing = false;
  let refreshAgain = false;
  let pending = false;
  let connectionFailed = false;
  let rider = '';
  let currentLobby = null;
  let activeRoom = null;
  const logKeys = new WeakMap();

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
    if (data !== undefined) {
      options.method = 'POST';
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(data);
    }
    const response = await fetch(app.dataset.api + path, options);
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

  function drawMessages(list, messages) {
    const key = messages.map(message => message.id + ':' + message.rider + ':' + message.phrase).join('|');
    if (logKeys.get(list) === key) return;
    logKeys.set(list, key);
    const nearEnd = list.scrollHeight - list.clientHeight - list.scrollTop < 56;
    const previous = new Map(Array.from(list.children).map(item => [item.dataset.key, item]));
    const keep = new Set();
    for (const message of messages) {
      const id = String(message.id);
      keep.add(id);
      if (previous.has(id)) continue;
      const row = document.createElement('li');
      row.dataset.key = id;
      const author = document.createElement('strong');
      author.textContent = message.rider;
      row.append(author, message.phrase);
      list.append(row);
    }
    for (const [id, row] of previous) {
      if (!keep.has(id)) row.remove();
    }
    if (!messages.length) {
      const row = document.createElement('li');
      row.textContent = 'Quiet in here. Start with a quick message.';
      list.append(row);
    }
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

  function schedule() {
    clearTimeout(timer);
    if (!document.hidden) timer = setTimeout(refresh, 4000);
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
        rider = data.rider;
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
      if (id && error.status === 404) {
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
    if (pending) return;
    pending = true;
    button.disabled = true;
    try {
      await task();
      showStatus('Connected as ' + rider);
      await refresh();
    } catch (error) {
      showStatus(error.status ? error.message : 'Could not reach the Race Room. Try again.', true);
    } finally {
      button.disabled = false;
      pending = false;
    }
  }

  function addPhrases(target, phrases, path) {
    for (const phrase of phrases) {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = phrase;
      button.addEventListener('click', () => action(button, () => api(path(), { phrase })));
      byId(target).append(button);
    }
  }

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
  addPhrases('race-public-phrases', publicPhrases, () => '/chat');
  addPhrases('race-lobby-phrases', roomPhrases, () => '/rooms/' + roomId() + '/chat');
  window.addEventListener('popstate', () => {
    resetRoom();
    refresh();
  });
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) clearTimeout(timer);
    else refresh();
  });
  refresh();
})();
