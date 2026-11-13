# Vanilla gear stats (in-game speed units)

Numbers from WtR_Flash's "Sonic Riders Advanced Gear Guide" on GameFAQs (2006, GameCube, measured in play):
https://gamefaqs.gamespot.com/gamecube/929721-sonic-riders/faqs/42167 . The guide itself is not in the repo
(its author does not allow re-hosting); a local copy lives in `docs/local/` on the machines that have it.
Top speed is with the character that loses none (Sonic for speed-type gear; see the guide for per-character losses:
Tails/Wave/NiGHTS -12, Cream -14, Rouge -11, Knuckles/AiAi/E-10R -7, Amy/Storm/Eggman -4, Jet -2).

Matches the racer level blocks (game-addresses.md): block +0x08 = top speed, +0x34 = boost (x216 = these units).
Blue Star in RAM: +0x08 162/162/180, +0x34 200/230/250, +0x30 (drift dash) 50/80/100, +0x28 boost cost 25/30/40 x1000.

| # | Gear | Top speed Lv1-2 | Lv3 | Boost Lv1 / Lv2 / Lv3 | Off-road |
|---|---|---|---|---|---|
| 1 | Blue Star | 162 | 180 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 2 | Yellow Tail | 150 | 165 | 200 / 230 / 250 | (Lv1) 80, (Lv2/3) 90 |
| 3 | Red Rock | 155 | 165 | 200 / 230 / 250 | (Lv1) 100, (Lv2/3) 105 |
| 4 | Pink Rose | 158 | 176 | 200 / 230 / 250 | (Lv1) 84, (Lv2/3) 94 |
| 5 | Type-J | 160 | 178 | 200 / 230 / 250 | (Lv1) 84, (Lv2/3) 94 |
| 6 | Type-S | 158 | 168 | 200 / 230 / 250 | (Lv1) 100, (Lv2/3) 106 |
| 7 | Type-W | 150 | 165 | 200 / 230 / 250 | (Lv1) 80, (Lv2/3) 90 |
| 8 | Temptation | 151 | 166 | 200 / 230 / 250 | (Lv1) 80, (Lv2/3) 90 |
| 9 | Smile | 148 | 163 | 200 / 230 / 250 | (Lv1) 79, (Lv2/3) 89 |
| 10 | High Booster | 157 | 175 | 250 / 300 / 350 | (Lv1) 80, (Lv2/3) 90 |
| 11 | Auto Slider | 157 | 175 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 12 | Powerful Gear | 162 |  | 200 / 230 / 250 | (Lv1) 155, (Lv2/3) 160 |
| 13 | Fastest | 202 | 219 | 230 / 240 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 14 | Turbo Star | 142 | 160 | 180 / 200 / 230 | (Lv1) 70, (Lv2/3) 80 |
| 15 | Speed Balancer | 177 | 195 | 200 / 230 / 250 | (Lv1) 75, (Lv2/3) 85 |
| 16 | Blue Star II | 167 | 184 | 210 / 235 / 250 | (Lv1) 75, (Lv2/3) 85 |
| 17 | Access | 152 | 170 | 180 / 210 / 230 | (Lv1) 80, (Lv2/3) 90 |
| 18 | Beginner | 157 | 175 | 200 / 230 / 250 | (Lv1) 80, (Lv2/3) 90 |
| 19 | Accelerator | 152 | 170 | 200 / 230 / 250 | (Lv1) 80, (Lv2/3) 90 |
| 20 | Trap Gear | 147 | 164 | N/A | (Lv1) 80, (Lv2/3) 90 |
| 21 | Light Board | 145 | 161 | 190 / 220 / 240 | (Lv1) 82, (Lv2/3) 91 |
| 22 | Slide Booster | 152 | 170 | N/A | (Lv1) 75, (Lv2/3) 85 |
| 23 | Legend | 152 | 170 | 200 / 230 / 250 | (Lv1) 75, (Lv2/3) 85 |
| 24 | Magic Carpet | 162 | 180 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 25 | Air Broom | 152 | 170 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 26 | Hovercraft | 172 | 190 | 210 / 240 / 260 | (Lv1) 90, (Lv2/3) 100 |
| 27 | Chaos Emerald | 182 |  | 250 | (Lv1) 95 |
| 28 | Faster | 182 | 200 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 29 | Gambler | 157 | 175 | 195 / 220 / 230 | (Lv1) 80, (Lv2/3) 90 |
| 30 | Power Gear | 162 | 180 | 200 / 230 / 250 | (Lv1) 135, (Lv2/3) 145 |
| 31 | Opa Opa | 162 | 180 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 32 | The Crazy | 162 |  | 230 | (Lv1) 100 |
| 33 | Night Sky | 150 | 165 | 200 / 230 / 250 | (Lv1) 80, (Lv2/3) 90 |
| 34 | BANANA | 155 | 165 | 200 / 230 / 250 | (Lv1) 100, (Lv2/3) 105 |
| 35 | E-gearG | 162 | 180 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 36 | E-gearR | 155 | 165 | 200 / 230 / 250 | (Lv1) 100, (Lv2/3) 105 |
| 37 | Channel5 | 162 | 180 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 38 | Berserker | 152 | 170 | 190 / 210 / 230 | (Lv1) 85, (Lv2/3) 95 |
| 39 | E-rider | 167 | 184 | 200 / 230 / 250 | (Lv1) 90, (Lv2/3) 100 |
| 40 | Air Tank | 152 | 170 | 190 / 210 / 230 | (Lv1) 80, (Lv2/3) 90 |
| 41 | Heavy Bike | 183 | 203 | 210 / 230 / 250 | (Lv1) 92, (Lv2/3) 103 |
| 42 | Destroyer | 152 | 170 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 43 | Omnipotence | 152 | 170 | 200 / 230 / 250 | (Lv1) 80, (Lv2/3) 90 |
| 44 | Cover-S | 162 | 180 | 200 / 230 / 250 | (Lv1) 87, (Lv2/3) 97 |
| 45 | Cover-F | 161 | 176 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 46 | Cover-P | 163 | 173 | 200 / 230 / 250 | (Lv1) 100, (Lv2/3) 106 |
| 47 | Hang-On | 162 | 180 | 200 / 230 / 250 | (Lv1) 85, (Lv 2/3) 95 |
| 48 | Super Hang-On | 162 | 180 | 200 / 230 / 250 | (Lv1) 85, (Lv2/3) 95 |
| 49 | Darkness | 162 | 180 | 200 / 230 / 250 | (Lv1) 80, (Lv2/3) 90 |
| 50 | Grinder | 158 | 168 | 200 / 230 / 250 | (Lv1) 95, (Lv2/3) 96 (...) |
| 51 | Advantage-S | 182 | 200 | 200 / 230 / 250 | (Lv1) 75, (Lv2/3) 85 |
| 52 | Advantage-F | 141 | 156 | 200 / 230 / 250 | (Lv1) 70, (Lv2/3) 80 |
| 53 | Advantage-P | 158 | 168 | 200 / 230 / 250 | (Lv1/2) 120, (Lv3) 125 |
| 54 | Cannonball | 132 | 150 | 170 / 180 / 200 | (Lv1) 70 (Lv2/3) 80 |
