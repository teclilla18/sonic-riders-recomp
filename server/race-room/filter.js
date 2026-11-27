// Race Room text rules, shared by the server (worker.js) and its tests.
// The website has a small copy of hasLink() in assets/js/race-room.js only to grey out the Send button;
// the server is the one that really refuses links.

const TLDS = [
  'com', 'net', 'org', 'gg', 'io', 'ly', 'me', 'co', 'xyz', 'tv', 'app', 'dev', 'info', 'biz', 'us', 'uk', 'ru',
  'cn', 'de', 'fr', 'br', 'jp', 'link', 'site', 'online', 'store', 'shop', 'live', 'fun', 'club', 'top', 'click',
  'to', 'be', 'sh', 'ai', 'cc', 'ws', 'tk', 'ml', 'ga', 'cf', 'gq', 'pw', 'in', 'eu', 'ca', 'au', 'mx', 'es',
  'it', 'nl', 'pl', 'se', 'fi', 'ch', 'at', 'cz', 'gl', 'lol', 'page', 'pro', 'win', 'bid', 'vip', 'world',
  'life', 'games', 'game', 'chat', 'social', 'news', 'blog', 'email', 'zip', 'mov', 'art', 'st', 'fm', 'im',
  'tw', 'id', 'ph', 'vn', 'kr', 'ar', 'cl', 'pe', 'tr', 'ua', 'kz', 'ir', 'ae', 'il', 'za', 'ng', 'nz', 'sg',
  'hk', 'nu', 'ms', 'gd', 'li', 'lt', 'lv', 'ee', 'sk', 'ro', 'hu', 'gr', 'pt', 'ie', 'dk', 'su', 'wtf',
  'tech', 'space', 'website', 'host', 'cloud', 'digital', 'network', 'stream', 'media', 'download', 'monster'
];
const DOMAIN = new RegExp('(?:^|[^\\p{L}\\p{N}_-])[\\p{L}\\p{N}-]{1,63}(?:\\.[\\p{L}\\p{N}-]{1,63})*\\.(?:' +
  TLDS.join('|') + ')(?![\\p{L}\\p{N}])', 'u');
const LINK_PATTERNS = [
  /[a-z][a-z0-9+.-]{1,15}:\/\//,          // http://, https://, ftp://, steam://...
  /(?:^|[^a-z0-9])www\d?\./,
  DOMAIN,                                   // example.com, sub.example.gg
  /\b(?:discord(?:app)?|dsc)\s*\.?\s*gg\b/, // "discord gg/abc"
  /\b\d{1,3}(?:\.\d{1,3}){3}\b/,            // IP addresses
  /(?:^|\s)\/?(?:r|u)\/[a-z0-9_]{3,}/,       // reddit-style r/name u/name
  /\b(?:t\.me|wa\.me|bit\s*ly|tinyurl|goo\s*gl)\b/
];

// Text as a link checker should see it: same-looking letters folded, hidden characters removed,
// "[.]", "(dot)", " dot " turned back into dots.
export function linkView(text) {
  return String(text).normalize('NFKC').toLowerCase()
    .replace(/[。｡․‧·∙⋅・]/g, '.')
    .replace(/\p{Cf}/gu, '')
    .replace(/\s*(?:\[\s*\.\s*\]|\(\s*\.\s*\)|\{\s*\.\s*\}|\[\s*dot\s*\]|\(\s*dot\s*\)|\{\s*dot\s*\}|<\s*dot\s*>)\s*/g, '.')
    .replace(/(\S)\s+dot\s+(?=\S)/g, '$1.');
}

export function hasLink(text) {
  const view = linkView(text);
  return LINK_PATTERNS.some(pattern => pattern.test(view));
}

// Cleans a chat message: one line, no control or direction-flipping characters, no "zalgo" stacks.
// Emoji sequences (ZWJ U+200D, variation selectors, skin tones, keycaps) stay intact.
export function cleanText(text) {
  return String(text ?? '')
    .replace(/[\u0000-\u0008\u000b-\u001f\u007f-\u009f]/g, '')
    .replace(/[\r\n\t]+/g, ' ')
    .replace(/[‪-‮⁦-⁩​‎‏⁠﻿­]/g, '')
    .replace(/(\p{M}{3})\p{M}+/gu, '$1')
    .replace(/ {2,}/g, ' ')
    .trim();
}

export const MAX_TEXT = 240; // code points

// Names: 3-20 letters, digits, spaces and _ . - ; never a link, never a staff-looking name.
const RESERVED = ['gamedev', 'admin', 'moderator', 'owner', 'official', 'teclilla', 'ridersrebuilt', 'ridersplus',
  'staff', 'system', 'developer', 'support', 'sega', 'sonicteam', 'raceroom'];
export function nameKey(name) {
  return String(name).normalize('NFKC').toLowerCase()
    .replace(/[0@]/g, 'o').replace(/[1!|]/g, 'l').replace(/3/g, 'e').replace(/4/g, 'a').replace(/[5$]/g, 's')
    .replace(/[^\p{L}\p{N}]/gu, '');
}
export function checkName(raw) {
  const name = cleanText(raw).normalize('NFC');
  const length = [...name].length;
  if (length < 3 || length > 20) return { error: 'Names are 3 to 20 characters.' };
  if (!/^[\p{L}\p{N}][\p{L}\p{N} _.\-]*$/u.test(name)) return { error: 'Use letters, numbers, spaces, _ . or - in names.' };
  if (hasLink(name)) return { error: 'Names can’t look like links.' };
  const key = nameKey(name);
  if (key.length < 2) return { error: 'Names need at least two letters or numbers.' };
  if (key === 'mod' || key === 'mods' || RESERVED.some(word => key.includes(word.replace(/ /g, '')))) {
    return { error: 'That name is reserved. Pick another one.' };
  }
  return { name, key };
}
