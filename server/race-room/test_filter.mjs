// node server/race-room/test_filter.mjs : quick checks of the link filter and name rules.
import { hasLink, checkName, cleanText } from './filter.js';
const links = ['https://x.co', 'go to example.com now', 'www.thing', 'discord.gg/abc', 'discord gg/abc', 'site[.]com',
  'site (dot) com', 'site dot com', 'ｅｘａｍｐｌｅ．ｃｏｍ', 'ex​ample.com', '192.168.0.1', 'bit.ly/x', 'join r/sonicriders',
  'mail me a@b.net', 'steam://run/1', 'youtu.be/abc', 'tiny.cc/x'];
const fine = ['gg everyone', 'good race!', 'v0.10.0 is out', 'lol. me too', 'ok so who hosts?', 'I got 3.5 seconds',
  'Same beta build?', '🏁🔥 let’s go', 'e.g. the PC one', 'Super Sonic is op...', 'Dr. Eggman', '10:30 pm', '👨‍👩‍👧 family'];
let bad = 0;
for (const t of links) if (!hasLink(t)) { bad++; console.log('MISSED link:', t); }
for (const t of fine) if (hasLink(t)) { bad++; console.log('FALSE link:', t); }
for (const n of ['Game Dev', 'gamedev', 'G4me_Dev', 'Admin', 'x.com', 'ab', 'Mod']) if (!checkName(n).error) { bad++; console.log('NAME allowed:', n); }
for (const n of ['Jet Fan', 'Wave_99', 'Storm-1', 'Sónic', 'さくら']) if (checkName(n).error) { bad++; console.log('NAME refused:', n, checkName(n).error); }
if (cleanText('👨‍👩‍👧') !== '👨‍👩‍👧') { bad++; console.log('emoji damaged'); }
console.log(bad ? bad + ' problem(s)' : 'all filter checks pass');
process.exit(bad ? 1 : 0);
