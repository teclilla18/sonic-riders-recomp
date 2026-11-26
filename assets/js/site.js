/* Riders Rebuilt website script. No dependencies. Every page works without it: the HTML already holds the
   snapshot made by tools/build_site.py; this file refreshes it from GitHub's public data when it can. */
(function () {
  'use strict';
  var REPO = 'teclilla18/sonic-riders-recomp';
  var API = 'https://api.github.com/repos/' + REPO;
  var RAW = 'https://raw.githubusercontent.com/' + REPO + '/main/';
  var FILES = {
    switch2: /Switch2.*\.zip$/, switch: /^SonicRiders-Switch-Setup-.*\.zip$/, pc: /PC-Setup.*\.zip$/,
    android: /Android-Setup.*\.zip$/, linux: /Linux-Setup.*\.zip$/, ios: /iOS-Setup.*\.zip$/
  };
  var TTL = 20 * 60 * 1000; // GitHub allows 60 anonymous API calls an hour; cache for 20 minutes
  document.documentElement.classList.add('js');

  function $all(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }
  function store(key, val) {
    try {
      if (val === undefined) return JSON.parse(localStorage.getItem(key) || 'null');
      localStorage.setItem(key, JSON.stringify(val));
    } catch (e) { return null; }
  }
  function cached(key, url, type) {
    var hit = store('rp:' + key);
    if (hit && Date.now() - hit.t < TTL) return Promise.resolve(hit.v);
    var ctl = window.AbortController ? new AbortController() : null;
    var timer = setTimeout(function () { if (ctl) ctl.abort(); }, 8000);
    return fetch(url, ctl ? { signal: ctl.signal } : {}).then(function (r) {
      clearTimeout(timer);
      if (!r.ok) throw new Error(r.status);
      return type === 'text' ? r.text() : r.json();
    }).then(function (v) { store('rp:' + key, { t: Date.now(), v: v }); return v; })
      .catch(function () { return hit ? hit.v : null; }); // stale data beats nothing
  }
  function fmtSize(n) { return n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB'; }
  function fmtDate(d) {
    if (!d) return '';
    var m = 'Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec'.split(' '), p = d.slice(0, 10).split('-');
    return m[+p[1] - 1] + ' ' + (+p[2]) + ', ' + p[0];
  }
  function setText(el, t) { if (el && t != null) el.textContent = t; }
  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }

  /* ---------------------------------------------------------------- releases, downloads */
  function applyReleases(list) {
    if (!Array.isArray(list) || !list.length) return;
    list = list.filter(function (r) { return !r.draft; });
    $all('[data-asset]').forEach(function (btn) {
      var alpha = btn.getAttribute('data-channel') === 'alpha';
      var re = new RegExp(btn.getAttribute('data-pattern'));
      var id = btn.getAttribute('data-asset');
      for (var i = 0; i < list.length; i++) {
        var r = list[i];
        if (!!r.prerelease !== alpha) continue;
        for (var j = 0; j < (r.assets || []).length; j++) {
          var a = r.assets[j];
          if (re.test(a.name)) {
            btn.href = a.browser_download_url;
            setText(document.querySelector('[data-asset-name="' + id + '"]'), a.name);
            setText(document.querySelector('[data-asset-meta="' + id + '"]'), r.tag_name + ' · ' + fmtSize(a.size));
            return;
          }
        }
      }
    });
    var total = 0;
    list.forEach(function (r) { (r.assets || []).forEach(function (a) { total += a.download_count || 0; }); });
    $all('[data-stat="downloads"]').forEach(function (el) { el.textContent = total.toLocaleString('en-US'); });
    [['stable', false], ['alpha', true]].forEach(function (k) {
      var r = list.filter(function (x) { return !!x.prerelease === k[1]; })[0];
      if (!r) return;
      $all('[data-release="' + k[0] + '"]').forEach(function (card) {
        var dl = 0;
        (r.assets || []).forEach(function (a) { dl += a.download_count || 0; });
        setText(card.querySelector('[data-rel-name]'), (r.name || r.tag_name).replace(/Riders Plus/gi, 'Riders Rebuilt'));
        setText(card.querySelector('[data-rel-tag]'), r.tag_name);
        var t = card.querySelector('[data-rel-date]');
        if (t) { t.textContent = fmtDate(r.published_at); t.setAttribute('datetime', (r.published_at || '').slice(0, 10)); }
        setText(card.querySelector('[data-rel-dl]'), dl.toLocaleString('en-US'));
        $all('[data-rel-name], [data-rel-link]', card).forEach(function (a) { a.href = r.html_url; });
      });
      $all('[data-latest="' + k[0] + '"]').forEach(function (el) { el.textContent = k[1] ? ((r.name || r.tag_name).replace(/Riders Plus/gi, 'Riders Rebuilt')).split(':')[0] : r.tag_name; });
    });
    // Easy-read guides name the current ZIP files (patterns match PLATFORMS in tools/build_site.py)
    $all('[data-file]').forEach(function (el) {
      var id = el.getAttribute('data-file'), p = FILES[id];
      if (!p) return;
      for (var i = 0; i < list.length; i++) {
        if (!!list[i].prerelease !== (id === 'switch2')) continue;
        var a = (list[i].assets || []).filter(function (x) { return p.test(x.name); })[0];
        if (a) { el.textContent = a.name; return; }
      }
    });
  }
  if (document.querySelector('[data-asset], [data-release], [data-stat], [data-latest], [data-file]')) {
    cached('releases', API + '/releases?per_page=40').then(applyReleases);
  }

  /* ---------------------------------------------------------------- social media (socials.json) */
  if (document.querySelector('[data-socials]')) {
    fetch('socials.json', { cache: 'no-cache' }).then(function (r) { return r.ok ? r.json() : null; }).then(function (items) {
      if (!Array.isArray(items)) return;
      items = items.filter(function (i) { return i && i.name && /^https:\/\//.test(i.url || ''); });
      $all('[data-socials="links"]').forEach(function (el) {
        el.innerHTML = items.map(function (i) { return '<a href="' + esc(i.url) + '" rel="me">' + esc(i.name) + '</a>'; }).join('');
      });
      $all('.social-slot').forEach(function (slot) {
        var parent = slot.parentNode, reveal = !!parent.querySelector('.comm.reveal');
        $all('[data-social]', parent).forEach(function (a) { a.remove(); });
        var html = items.map(function (i) {
          return '<a class="comm panel' + (reveal ? ' reveal in-view' : '') + '" href="' + esc(i.url) + '" rel="me" data-social><h3>' + esc(i.name) +
            '</h3><p>' + esc(i.about || '') + ' ' + esc(i.handle || '') + '</p><span class="go">Follow →</span></a>';
        }).join('');
        slot.insertAdjacentHTML('afterend', html);
      });
    }).catch(function () {});
  }

  /* ---------------------------------------------------------------- Road to v1.0 */
  function mdInline(s) {
    return esc(s)
      .replace(/`([^`]+)`/g, '<code>$1</code>')
      .replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, '<a href="$2">$1</a>')
      .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
      .replace(/(^|[^\w\/&])#(\d+)\b/g, '$1<a href="https://github.com/' + REPO + '/issues/$2">#$2</a>');
  }
  var areasEl = document.querySelector('[data-progress-areas]');
  if (areasEl) {
    Promise.all([cached('mkprog', RAW + 'tools/make_progress.py', 'text'), cached('readme', RAW + 'README.md', 'text')])
      .then(function (res) {
        var py = res[0], md = res[1];
        if (!py) return;
        var block = (py.split('AREAS')[1] || '').split(']')[0];
        var re = /\(\s*"([^"]+)",\s*(\d+),\s*(\d+)\s*\)/g, m, areas = [];
        while ((m = re.exec(block))) areas.push({ label: m[1], weight: +m[2], done: +m[3] });
        if (!areas.length) return;
        var left = {};
        (md || '').split('\n').forEach(function (line) {
          var c = line.trim().replace(/^\||\|$/g, '').split('|').map(function (x) { return x.trim(); });
          if (c.length === 3 && /%$/.test(c[1])) left[c[0]] = c[2];
        });
        var tw = 0, sum = 0;
        areas.forEach(function (a) { tw += a.weight; sum += a.weight * a.done; });
        var overall = Math.round(sum / (tw || 1));
        $all('[data-progress-overall]').forEach(function (el) { el.textContent = overall; });
        var road = document.getElementById('road');
        if (road) road.style.setProperty('--v', overall);
        var v = /^VERSION\s*=\s*"([^"]+)"/m.exec(py), u = /^UPDATED\s*=\s*"([^"]+)"/m.exec(py);
        if (v) setText(document.querySelector('[data-progress-version]'), v[1]);
        if (u) setText(document.querySelector('[data-progress-updated]'), fmtDate(u[1]));
        areasEl.innerHTML = areas.map(function (a) {
          return '<li class="area" style="--v:' + a.done + '"><div class="area-top"><span class="area-name">' + esc(a.label) +
            '</span><span class="area-w">weight ' + a.weight + ' %</span><span class="hud-num area-pct">' + a.done +
            ' %</span></div><div class="gauge in-view" role="img" aria-label="' + esc(a.label) + ': ' + a.done +
            ' percent done"><i></i></div><p class="area-left">' + (left[a.label] ? mdInline(left[a.label]) : '') + '</p></li>';
        }).join('');
      });
  }

  /* ---------------------------------------------------------------- suggest a download for this device */
  var plats = $all('[data-platform]');
  if (plats.length) {
    var ua = navigator.userAgent || '', guess = null;
    if (/iPhone|iPad|iPod/.test(ua) || (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1)) guess = 'ios';
    else if (/Android/.test(ua)) guess = 'android';
    else if (/Windows/.test(ua)) guess = 'pc';
    else if (/Linux|CrOS/.test(ua) && !/Android/.test(ua)) guess = 'linux';
    else if (/Macintosh/.test(ua)) guess = 'ios';
    var card = guess && document.querySelector('[data-platform="' + guess + '"]');
    if (card) {
      card.classList.add('is-rec');
      var rec = card.querySelector('.plat-rec');
      if (rec) {
        rec.hidden = false;
        if (guess === 'ios' && /Macintosh/.test(ua) && !(navigator.maxTouchPoints > 1)) rec.textContent = 'On a Mac? This builds the iPhone / iPad app';
      }
    }
  }

  /* ---------------------------------------------------------------- tabs (easy-read guides) */
  $all('[data-tabs]').forEach(function (bar) {
    var key = 'rp:tab:' + bar.getAttribute('data-tabs');
    var tabs = $all('[role="tab"]', bar);
    function show(id, focus) {
      if (!id || !tabs.some(function (t) { return t.getAttribute('data-dev') === id; })) return false;
      var found = false;
      tabs.forEach(function (t) {
        var on = t.getAttribute('data-dev') === id;
        found = found || on;
        t.setAttribute('aria-selected', on ? 'true' : 'false');
        t.tabIndex = on ? 0 : -1;
        var panel = document.getElementById(t.getAttribute('aria-controls'));
        if (panel) panel.hidden = !on;
        if (on && focus) t.focus();
      });
      if (found) store(key, id);
      return found;
    }
    tabs.forEach(function (t, i) {
      t.addEventListener('click', function () {
        show(t.getAttribute('data-dev'));
        if (history.replaceState) history.replaceState(null, '', '#' + t.getAttribute('data-dev'));
      });
      t.addEventListener('keydown', function (e) {
        var d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
        if (d) { var n = tabs[(i + d + tabs.length) % tabs.length]; show(n.getAttribute('data-dev'), true); e.preventDefault(); }
      });
    });
    var hash = location.hash.slice(1);
    if (!(hash && show(hash))) show(store(key) || tabs[0].getAttribute('data-dev'));
    window.addEventListener('hashchange', function () { show(location.hash.slice(1)); });
    $all('a[data-go]').forEach(function (a) {
      a.addEventListener('click', function () { show(a.getAttribute('data-go')); });
    });
  });

  /* version picker on the update guide */
  $all('[data-versions]').forEach(function (box) {
    var btns = $all('button[data-ver]', box), scope = box.parentNode;
    function pick(v) {
      btns.forEach(function (b) { b.setAttribute('aria-pressed', b.getAttribute('data-ver') === v ? 'true' : 'false'); });
      $all('[data-ver-panel]', scope).forEach(function (p) { p.hidden = p.getAttribute('data-ver-panel') !== v; });
    }
    btns.forEach(function (b) { b.addEventListener('click', function () { pick(b.getAttribute('data-ver')); }); });
    pick('new');
  });

  /* tick steps off */
  $all('.easy-steps > li').forEach(function (li) {
    li.tabIndex = 0;
    function flip(e) { if (e.target.closest('a')) return; li.classList.toggle('done'); }
    li.addEventListener('click', flip);
    li.addEventListener('keydown', function (e) { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); flip(e); } });
  });

  /* booklet checkpoints, remembered per device */
  var checks = $all('[data-check]');
  function countChecks() {
    var n = 0;
    checks.forEach(function (c) {
      if (c.checked) n++;
      var sec = c.closest('section');
      var link = sec && document.querySelector('.booklet-toc a[href="#' + sec.id + '"]');
      if (link) link.classList.toggle('ticked', c.checked);
    });
    setText(document.querySelector('[data-check-count]'), n);
  }
  checks.forEach(function (c) {
    var k = 'rp:check:' + c.getAttribute('data-check');
    c.checked = !!store(k);
    c.addEventListener('change', function () { store(k, c.checked); countChecks(); });
  });
  if (checks.length) countChecks();

  /* copy buttons */
  $all('[data-copy-prev]').forEach(function (b) {
    b.addEventListener('click', function () {
      var pre = b.previousElementSibling;
      if (!pre || !navigator.clipboard) return;
      navigator.clipboard.writeText(pre.textContent).then(function () {
        var inner = b.querySelector('.btn-in'), old = inner.textContent;
        inner.textContent = 'Copied';
        setTimeout(function () { inner.textContent = old; }, 1600);
      });
    });
  });

  /* before / after slider */
  $all('.compare').forEach(function (c) {
    var input = c.querySelector('input');
    function upd() { c.style.setProperty('--pos', input.value + '%'); }
    input.addEventListener('input', upd);
    upd();
  });
  $all('[data-compare-set]').forEach(function (b) {
    b.addEventListener('click', function () {
      var c = document.getElementById(b.getAttribute('data-compare-target'));
      var n = b.getAttribute('data-compare-set');
      c.querySelector('.before').src = 'assets/img/compare/original-' + n + '.jpg';
      c.querySelector('.after img').src = 'assets/img/compare/recomp-' + n + '.jpg';
      $all('[data-compare-set]').forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); });
    });
  });

  /* reveal + gauges when scrolled into view */
  var watch = $all('.reveal, .gauge');
  if ('IntersectionObserver' in window) {
    var io = new IntersectionObserver(function (es) {
      es.forEach(function (e) { if (e.isIntersecting) { e.target.classList.add('in-view'); io.unobserve(e.target); } });
    }, { rootMargin: '0px 0px -8% 0px' });
    watch.forEach(function (el) { io.observe(el); });
  } else {
    watch.forEach(function (el) { el.classList.add('in-view'); });
  }
})();
