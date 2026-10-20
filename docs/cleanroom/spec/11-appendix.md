# 11 — Appendix: compatibility data, quirk index, verification plan

## A. Built-in message table (strings.dat defaults)

When the game folder contains no `strings.dat`, the runtime uses the following 150 texts. When the
file exists, its lines replace this table completely (see `02-data-formats.md`, strings.dat). Texts are
shown here in UTF-8; the runtime stores and displays them as Shift_JIS. Leading and trailing spaces are
significant (rows 100, 101, 112, 113). `未使用` ("unused") rows are never displayed except where a
chapter says otherwise. Menu and command rows below are numbered from 0, as in the chapters.

Every known game ships its own `strings.dat` (the editor writes all 150 lines; all 31 sample games
have one: 150 lines, each ended by a single LF, no CR), so these defaults are rarely seen.
Many games differ from them; in particular most use `泊まる` / `泊まらない` **without** the spaces of
rows 112/113 (24 of the 31 samples have `泊まる` in row 112), so their inn command window is 128 wide
at x = 256, not 160 at x = 240 as in the default-string example of `07-menus-shop-inn.md` §9. Layout
checks must use the game's own `strings.dat`.

Usage notes refer to the chapters where each text is used; "suffix" means the text is appended directly
after a name or number (for example strings[45] gives `アルフは気絶した`).

| n | default text | used for |
|---|---|---|
| 0 | `アイテム` | main menu row 0; battle command row 2 (アイテム) |
| 1 | `魔法` | main menu row 1; battle command row 1 (魔法) |
| 2 | `装備` | main menu row 2 |
| 3 | `ステータス` | main menu row 3 |
| 4 | `並び替え` | main menu row 4 |
| 5 | `戦う` | battle command row 0 |
| 6 | `逃げる` | battle command row 3 |
| 7 | `自動戦闘` | battle command row 4 |
| 8 | `未使用` | unused |
| 9 | `未使用` | unused |
| 10 | `武器` | equipment slot 0 name (weapon) |
| 11 | `鎧` | equipment slot 1 name (armour) |
| 12 | `盾` | equipment slot 2 name (shield) |
| 13 | `兜` | equipment slot 3 name (helmet) |
| 14 | `その他` | equipment slot 4 name (other) |
| 15 | `未使用` | unused |
| 16 | `未使用` | unused |
| 17 | `未使用` | unused |
| 18 | `未使用` | unused |
| 19 | `未使用` | unused |
| 20 | `正常` | state name: normal |
| 21 | `毒` | state name: poison |
| 22 | `麻痺` | state name: paralysis |
| 23 | `沈黙` | state name: silence |
| 24 | `混乱` | state name: confusion |
| 25 | `眠る` | state name: sleep |
| 26 | `気絶` | state name: knocked out (shown whenever HP < 1, and on the status screen when 0x20 is the highest set condition bit) |
| 27 | `未使用` | state name for state bit 0x40 |
| 28 | `未使用` | state name for state bit 0x80 |
| 29 | `未使用` | unused |
| 30 | `は毒に冒された` | poison inflicted |
| 31 | `はすでに毒に冒されている` | already poisoned |
| 32 | `の体から毒が消えた` | poison cured |
| 33 | `は麻痺した` | paralysis inflicted |
| 34 | `はすでに麻痺している` | already paralysed |
| 35 | `は麻痺から回復した` | paralysis cured |
| 36 | `は沈黙した` | silence inflicted |
| 37 | `はすでに沈黙している` | already silenced |
| 38 | `の沈黙が解けた` | silence cured |
| 39 | `は混乱した` | confusion inflicted |
| 40 | `はすでに混乱している` | already confused |
| 41 | `は正気を取り戻した` | confusion cured |
| 42 | `は眠らされた` | sleep inflicted |
| 43 | `はすでに眠っている` | already asleep |
| 44 | `は目を醒ました` | woke up / sleep cured |
| 45 | `は気絶した` | knocked out (battle damage, magic, poison on the map) |
| 46 | `は復帰した` | revived |
| 47 | `失敗！` | revive failed (prefix) |
| 48 | `は気絶している` | revive failed (suffix). (When an attacked target is already down, `08-battle.md` §4.1, the text is prepared but only the name is shown.) |
| 49 | `は麻痺していて動けない` | turn skipped: paralysed |
| 50 | `は眠っている` | turn skipped: asleep |
| 51 | `は混乱している` | acting while confused |
| 52 | `未使用` | unused |
| 53 | `未使用` | unused |
| 54 | `未使用` | unused |
| 55 | `未使用` | unused |
| 56 | `未使用` | unused |
| 57 | `未使用` | unused |
| 58 | `未使用` | unused |
| 59 | `未使用` | unused |
| 60 | `の呪文を唱えた` | magic cast message suffix |
| 61 | `しかし魔法は封じられている` | caster silenced |
| 62 | `ＭＰが足りない！` | not enough MP |
| 63 | `しかし不思議な力で呪文はかき消された！` | teleport/escape magic nullified (escape forbidden) |
| 64 | `しかし何も起こらなかった` | no effect |
| 65 | `しかし効果はなかった` | target is down |
| 66 | `には効果がなかった` | no effect on the target |
| 67 | `には意味がなかった` | nothing to heal or cure |
| 68 | `未使用` | unused |
| 69 | `未使用` | unused |
| 70 | `モンスターがあらわれた！` | battle start message |
| 71 | `会心の一撃！` | critical hit on a monster |
| 72 | `痛恨の一撃！` | critical hit on a party member |
| 73 | `はヒラリと身をかわした` | party member evades (1 of 3) |
| 74 | `は素早くよけた` | party member evades (2 of 3) |
| 75 | `は敵の攻撃を見切った` | party member evades (3 of 3) |
| 76 | `は完全に防御した` | party member takes no damage (1 of 3) |
| 77 | `は攻撃を撥ね返した` | party member takes no damage (2 of 3) |
| 78 | `はダメージを受けない` | party member takes no damage (3 of 3) |
| 79 | `未使用` | unused |
| 80 | `はヒラリと身をかわした` | monster evades (1 of 3) |
| 81 | `は素早くよけた` | monster evades (2 of 3) |
| 82 | `はすばしっこく逃げ回る` | monster evades (3 of 3) |
| 83 | `には歯が立たない！` | monster takes no damage (1 of 3) |
| 84 | `には全く効いていない！` | monster takes no damage (2 of 3) |
| 85 | `にダメージを与えられない！` | monster takes no damage (3 of 3) |
| 86 | `ﾎﾟｲﾝﾄのﾀﾞﾒｰｼﾞを受けた` | damage to a party member (suffix) |
| 87 | `ﾎﾟｲﾝﾄのﾀﾞﾒｰｼﾞを与えた` | damage to a monster (suffix) |
| 88 | `をやっつけた！` | monster defeated |
| 89 | `未使用` | unused |
| 90 | `アイテムを持っていません` | pop-up: no items (item menu, shop sell) |
| 91 | `装備できるアイテムを持っていません` | pop-up: nothing to equip |
| 92 | `戦闘中に使用できるアイテムを持っていません` | pop-up: no battle items |
| 93 | `戦闘時しか使用できません` | pop-up: item only usable in battle |
| 94 | `魔法を覚えていません` | pop-up: no magic (battle) |
| 95 | `フィールド上の魔法を覚えていません` | pop-up: no field magic |
| 96 | `ここでは使えません` | pop-up: teleport not possible here |
| 97 | `移動先がありません` | pop-up: no teleport destinations |
| 98 | `未使用` | unused |
| 99 | `未使用` | unused |
| 100 | ` 購入する ` | shop command: buy |
| 101 | ` 売却する ` | shop command: sell |
| 102 | `やめる` | shop command: leave |
| 103 | `何をお買い上げになりますか？` | shop buy prompt |
| 104 | `何をお売りになりますか？` | shop sell prompt |
| 105 | `ほかにもご用がおありでしょうか` | shop prompt after a deal |
| 106 | `売るアイテムはありません` | pop-up when the buy list is empty (quirk: the text talks about selling) |
| 107 | `そのアイテムはもう持てません` | buy refused: item count at maximum |
| 108 | `お金が足りません` | buy refused: not enough money |
| 109 | `をお買い上げですね。` | formatted after a purchase but never shown |
| 110 | `そのｱｲﾃﾑを売ることはできません` | sell refused |
| 111 | `ですね。` | formatted after a sale but never shown |
| 112 | ` 泊まる ` | inn command: stay |
| 113 | ` 泊まらない ` | inn command: do not stay |
| 114 | `気の毒だけどお金が足りないようだね。` | inn: not enough money |
| 115 | `未使用` | unused |
| 116 | `未使用` | unused |
| 117 | `未使用` | unused |
| 118 | `未使用` | unused |
| 119 | `未使用` | unused |
| 120 | `を装備した` | equip message suffix |
| 121 | `を使った` | item use message suffix |
| 122 | `を使おうと思ったが、どこにも見当たらなかった` | item gone before it could be used |
| 123 | `は逃げ出した！` | party escape attempt suffix |
| 124 | `逃げられない！` | escape forbidden |
| 125 | `しかし敵に回り込まれた！` | escape failed |
| 126 | `連続攻撃！` | second hit of a double attack |
| 127 | `は様子を見ている` | monster waits |
| 128 | `は逃げ出した` | monster flees |
| 129 | `モンスターはなにか落としていった` | item drop announcement |
| 130 | `を手に入れた！` | item obtained suffix |
| 131 | `Go Go Go!` | message window text after every completed battle turn |
| 132 | `は毒に冒されている` | poison damage at the end of a battle turn |
| 133 | `の魔法を覚えた` | magic learned on level up (special rule when this text is 未使用, see 08-battle.md) |
| 134 | `未使用` | unused |
| 135 | `未使用` | unused |
| 136 | `未使用` | unused |
| 137 | `未使用` | unused |
| 138 | `未使用` | unused |
| 139 | `未使用` | unused |
| 140 | `未使用` | unused |
| 141 | `未使用` | unused |
| 142 | `未使用` | unused |
| 143 | `未使用` | unused |
| 144 | `未使用` | unused |
| 145 | `未使用` | unused |
| 146 | `未使用` | unused |
| 147 | `未使用` | unused |
| 148 | `未使用` | unused |
| 149 | `未使用` | unused |

## B. Built-in sound effects

Sound effects referenced by number (battle animations, damage) are played from these file names, looked
up in the game folder like every other sound (see `09-audio-movies.md`). The runtime does not ship the
files; games include the ones they need, a missing file is silently not played.

| n | file | n | file | n | file | n | file |
|---|---|---|---|---|---|---|---|
| 0 | `flamer.wav` | 14 | `boom.wav` | 28 | `paku.wav` | 42 | `footstp3.wav` |
| 1 | `sword.wav` | 15 | `noizehit.wav` | 29 | `sludge.wav` | 43 | `latch.wav` |
| 2 | `metal.wav` | 16 | `snarehit.wav` | 30 | `arrrg.wav` | 44 | `obakedoa.wav` |
| 3 | `sword3.wav` | 17 | `powerup.wav` | 31 | `monster3.wav` | 45 | `warpout.wav` |
| 4 | `arrowhit.wav` | 18 | `electro.wav` | 32 | `swordhai.wav` | 46 | `wibble.wav` |
| 5 | `ropewhip.wav` | 19 | `bomb.wav` | 33 | `glsmash.wav` | 47 | `hitpoint.wav` |
| 6 | `fastarw.wav` | 20 | `explos3.wav` | 34 | `mizu.wav` | 48 | `healer.wav` |
| 7 | `kyudo.wav` | 21 | `electrix.wav` | 35 | `close.wav` | 49 | `refresh1.wav` |
| 8 | `ninjahit.wav` | 22 | `thunder.wav` | 36 | `creak.wav` | 50 | `zap64.wav` |
| 9 | `shot1.wav` | 23 | `thunder2.wav` | 37 | `creak2.wav` | 51 | `zapper.wav` |
| 10 | `knock.wav` | 24 | `monster.wav` | 38 | `creakdoa.wav` | 52 | `error.wav` |
| 11 | `punch.wav` | 25 | `monster2.wav` | 39 | `doashut3.wav` |  |  |
| 12 | `whack.wav` | 26 | `crunch.wav` | 40 | `doorshut.wav` |  |  |
| 13 | `banger.wav` | 27 | `magic.wav` | 41 | `footstep.wav` |  |  |

Sound effects used by name: `punch.wav` (every hit), `zap64.wav` (monster attack jump), `wibble.wav`
(teleport/escape magic).

## C. Hard-coded texts

Texts the runtime displays that are not in the message table (all must be reproduced exactly; the
half-width katakana are half-width in the original and must stay so):

| where | text |
|---|---|
| title menu | `はじめから` / `続きから` |
| main menu, last row | `システム` |
| system menu rows | `ゲームを保存する` / `ゲームを読み込む` / `スクリーンモードの変更` / `バージョン情報` / `終了する` |
| battle status headers | `Name`, `Hp`, `Mp`, `Status` |
| status screen | `Level %d`, `Hp:`, `Mp:`, `経験値:`, `ﾚﾍﾞﾙUPまで:`, `攻撃力:`, `防御力:`, `すばやさ:`, `知力:`, `---` (empty equipment slot), `<slot name>:`, `<coin name>:` |
| lists | `%dpts.` (magic cost), `(%d)` (item count), page indicator `(`, `%d`, `/%d )` |
| party lists (HP/MP columns) | `HP`, `MP`, `/` |
| party order display | `%d` (next position number), `%d:%s` (placed member), ` ` (one space, an undone position) |
| inn | `%s（一泊%d%s）` (full-width parentheses) |
| shop | `%s （所持金: %d%s）` (buy, full-width parentheses after a half-width space), `%s (所持金: %d%s)` (sell, half-width) |
| equipment | `%sは%s%s`, `%sが%dポイント上がった\n`, `%sが%d ポイント下がった\n`, stat names `攻撃力`, `魔法防御力`, `防御力` |
| item/magic use | `%sは%s%s`, `%sのHPが回復した`, `%sのMPが回復した`, `%sの%sが%dポイント上がった`, `\n攻撃力が%dポイント上がった`, `\n防御力が%dポイント上がった`, `\n最大ＨＰが%dポイント上がった`, `\n最大ＭＰが%dポイント上がった`, stat names `最大ＨＰ`, `最大ＭＰ`, `力`, `体力`, `知力`, `素早さ`, `運` |
| battle | `%sの攻撃`, `%sたち%s`, `%d%s獲得\n`, `%sは%dpointの経験値を獲得`, `生き残ったﾒﾝﾊﾞｰはそれぞれ %dpointの経験値を獲得`, `それぞれ %dpointの経験値を獲得`, `%sはレベルが上がった!`, `レベルが %d から %d に上がった\n`, `%sの攻撃力が下がった`, `%sの防御力が下がった`, `%sの素早さが下がった`, `%sの攻撃力が上がった`, `%sの防御力が上がった`, `%sの素早さが上がった`, `%sは %s の魔法を覚えた`, `%sは %s %s` |
| save/load | dialog titles `ゲームを保存する` / `ゲームを読み込む`, file type `RPGｹﾞｰﾑﾃﾞｰﾀ (*.sav)`, errors `データを書き込むことができません`, `ファイルを開くことができません`, `現バージョンでこのデータを\n読み込むことはできません` (message box title `RPGﾂｸｰﾙ95`) |
| missing files | `ﾌｧｲﾙ[%s]が見つかりません` (half-width katakana ﾌｧｲﾙ; message box, title `RPGﾂｸｰﾙ95`, error icon) |
| game file cannot be opened | `Can't open file!!!` (message box, title = the file's path, `02-data-formats.md`) |
| event file unreadable | `ｲﾍﾞﾝﾄﾌｧｲﾙを開けません` (message box, title `RPGﾂｸｰﾙ95`, `02-data-formats.md`) |
| start-up refusal | `このプログラムは単独では動作しません。` (title `RPGﾂｸｰﾙ95 - Game.exe`, `01-runtime.md`) |
| display set-up failures | `DirectDrawの作成に失敗しました。` + line feed + `ｹﾞｰﾑの実行を中止します` (title `RPGﾂｸｰﾙ95`); on the other failure paths, with title `DirectDraw Error` and an exclamation icon: `DirectDrawの作成に失敗しました`, `ﾌﾙｽｸﾘｰﾝﾓｰﾄﾞにすることができません`, `ｸﾞﾗﾌｨｯｸﾄﾞﾗｲﾊﾞがﾌﾙｽｸﾘｰﾝﾓｰﾄﾞに対応していません。` + line feed + `ｳｲﾝﾄﾞｳﾓｰﾄﾞにして再実行してください` |
| closing the program window | `ゲームを終了しますか` (Yes/No, question icon, title = game folder name, `01-runtime.md`) |
| version information | `ＲＰＧツクール９５`, `Runtime Routine  ver 1.23` (two spaces, `06-windows-text.md` §12), `Copyright (c) 1997 by ASCII Corporation`, `Copyright (c) 1997 by You & Tsune`, `All Rights Reserved.` |
| "stop music" name | `演奏停止` (a music name meaning "no music") |

`%s`, `%d` stand for a text and a decimal number inserted at that place, in the order given in the
chapter that uses the text; `\n` is a line break.

## D. Quirk index

All behaviours marked **(quirk)** in the chapters, collected here as a checklist. Section numbers (§) in each
list refer to the chapter named in its heading; the chapter text is authoritative.

### 01 — Runtime: program structure, main loop, input, waiting, random numbers, start-up (`01-runtime.md`)

* §1 — an escape/teleport effect used inside a battle teleports at the battle end without setting the pass-abort flag.
* §2.1 — with several `.RPG` files the last one in folder order is used.
* §2.2 — a file `suicide.exe` in the game folder makes the runtime refuse to run.
* §2.4 — if ITEM.DAT is missing, MAGIC.DAT is not read either.
* §2.5 — an empty start party is treated as defeated: event 99999 runs on every pass.
* §3.2 — the key-press handler runs a full message-processing pass before acting on its own key.
* §4.2 — in the map state a key press does not clear the release latch.
* §4.2 — Escape sets the confirm flag (ends waits) whenever the map does not have control.
* §4.2, §4.7 — the activation of the focused window is queued behind the key, so an inactive window ignores the key but becomes active.
* §4.3 — mouse movement, polling an un-held direction key and an idle joystick all re-arm the release latch.
* §4.5 — an Enter/Space press made while the hero is mid-step is forgotten.
* §4.7 — the joystick's synthetic keys prefer up/down over left/right; walking prefers up > left > down > right.
* §4.9 — a left click anywhere on the game area sets the confirm flag regardless of the latch.
* §4.9 — a left click outside the menu windows closes the whole field menu.
* §4.9 — double click and right+left click use the action button without movement-control, route or latch checks.
* §4.9 — right click opens the menu even while the hero is mid-step.
* §4.9 — mouse input over a deactivated window is handled as input on the game area outside windows.
* §4.9 — a teleport does not end mouse auto-walk; the hero then keeps turning right until a key or button is pressed.
* §5.1 — Wait(n) with n < 0 returns immediately.
* §5.2 — holding Escape freezes the game at the end of each hero step until it is released.
* §8 — in the autorun step, a pass-abort flag set by an autorun vehicle only ends the vehicle loop; step completion still runs.
* §8 — the end-of-pass page refresh skips the vehicles when the current map has no ordinary map events.
* §9.3 — a title image larger than the screen is shrunk to fit (per axis), not cropped.
* §9.3 — any end of the title-menu wait other than choosing `続きから` (Escape, a click outside the menu) starts a new game.
* §9.5 — after loading from the title the saved palette is shown as it is; the stored tint is not re-applied.
* §10 — a game-over event that does not end the game or revive the party runs again on every pass.
* §12 — switching the screen mode restarts the current music from the beginning.

### 02 — Data formats and file lookup (`02-data-formats.md`)

* §2.1 With several `.rpg` files the last one of the directory enumeration is used.
* §2.4 Base names are found by a byte-wise search for 0x5C, so Shift_JIS trail bytes 0x5C cut names
  (`カーソル1.wav` → `ル1.wav`); games ship files under such cut names.
* §2.4 Built-in sound effects are looked up in the game folder, not in a SYS\SE folder.
* §2.5 Line reads remove the last byte whatever it is: CR of CR LF stays; a last line without LF loses a
  character; lines over 255 bytes are split.
* §3.2 The game file's header is not checked; two header spellings exist.
* §3.2 The version text continues into the header bytes at 0x05..0x1C if the 5 version bytes contain no NUL.
* §3.3 Pre-0.96 files have 11 audio entries; slot 11 then has no name (nothing is played; a save writes `(null)`).
* §3.5 Actor start values are used as stored, never recomputed from the curves.
* §3.7 A 21-byte map name runs on into the depth field.
* §3.7 Map ids are not range-checked by the original.
* §3.8 Tile runs past the last cell are not guarded; at end of file the last value is reused.
* §3.9 A game file that cannot be opened produces a message and then a failure (there are no defaults).
* §4.4 0x1A in the string list is a terminator unless it follows index 25 and the size is not exhausted; a
  string with index 26 is counted one byte too long.
* §4.4 A duplicate string index ends the list without failure; the rest is read as command move routes.
* §4.4 A string list larger than the header size makes the event unrunnable (it is still placed).
* §4.4 The strings of a failed list stay stored and disturb the string list of the next event run.
* §5 Missing lines of strings.dat give progressively shortened copies of the previous string.
* §5 A long line in strings.dat shifts all later strings.
* §6.1 Monster names may use up to 20 bytes (into the bytes after the 18-byte name area).
* §6.3 ITEM.DAT missing prevents MAGIC.DAT from being read.
* §6.6 Dummy MONSTPIC.DAT files (event-file copies) are accepted; truncated records still count.
* §6.6 Monster picture widths not divisible by 4 are sheared.
* §7 Attribute tables are read once, from the start map's chipset names; a name with a drive letter is used as
  an absolute path.
* §8.1 The battle palette reload uses `BG01.BMP` regardless of the current background.
* §8.1 Chipset image names are not reduced to base names (a drive letter makes them unloadable).
* §8.1 Image sizes are not checked; tiles and sprites reaching outside a smaller image are not drawn.
* §9 Music and jingles choose MIDI / WAV / CD audio from the file name, not from the type field.
* §8.2 Top-down bitmaps, 108/124-byte headers and 16/32-bit bitmaps without colour table are rejected by the
  original.

### 03 — Display: the 8-bit screen model, palettes, effects, transitions, pictures, map composition (`03-display.md`)

* §1.2 — A surface copy whose source or destination rectangle is not completely inside its surface copies nothing at all (UNCERTAIN, display-system behaviour); affects small chipset/chara images and monsters beyond the screen edge.
* §2.3 — A palette source with fewer than 256 colours (1-, 4-bit images, 255-colour tables) changes only its first N work entries; the rest keep the previous source's colours; true-colour images change nothing.
* §2.4 — While a battle is in progress, "reload map palette" loads `BG01.BMP`, not the current battle background.
* §2.5 — After an uncommitted palette load, mode 4 sets nothing when the tinted result equals the old committed palette, leaving untinted colours on screen (battles/maps with the same palette as `CHARA.BMP` under a stored tint).
* §2.5 — After loading a game from the title, the save file's committed palette is shown as it is; the stored tint is not re-applied until the next palette reload.
* §3.1 — Images are converted against the realised palette at load time, which may be tinted or faded to black.
* §3.3 — Chipsets use the chara sheet's colour key, not their own top-left pixel.
* §4.2 — Colour-mode tint channels above 255 wrap modulo 256 (percentages above 200), e.g. the battle poison flash (255,0,0,0).
* §4.2 — Grey-mode luminance is one less than S/30 when S is a positive multiple of 30 (floating-point truncation with constants stored too small).
* §4.3 — The *changed* test compares the unwrapped values, so overflowing channels always count as changed.
* §4.5 — Shakes do not clear the uncovered strip of the screen.
* §4.6 — Modes 4/5/7 store the tint even when the palette did not change.
* §6.2 — Dissolves 1–3 never copy the remainder strip of a rectangle that is not a multiple of the block size.
* §6.2 — Show 13 ends with a full-screen present even for a picture rectangle.
* §6.2 — Show 14 shows system-colour pixels at full brightness before the fade-in.
* §6.3 — Hide 12 without blacken shows the whole back buffer at the first ring and then only delays.
* §6.3 — Hide 14 sets all 256 entries (including system colours) to black for a moment and always blackens the back buffer.
* §7.4 — "Keep aspect ratio" of fitted pictures is tied to the transparency bit; opaque fitted pictures are stretched.
* §7.5 — Erase screen 12 over a picture reveals the map at once then waits; Erase screen 14 over a picture leaves a black screen in state MAP until the next tick.
* §7.8 — A title or logo image larger than the screen is shrunk to fit per axis, not cropped.
* §8.1 — Chipsets are reloaded only at start-up, after battles, on screen-mode switch and on surface restore, never on a map change or game load.
* §8.2 — A tile or strip whose source lies outside a small chipset image is not drawn; the back buffer keeps its old pixels there.
* §9 — Window darkening factors 0.7 and 0.6 give one less for exact multiples (truncation in extended precision).
* §10 — A battle background smaller than 640 × 480 leaves map tiles visible around it.
* §10 — A monster whose rectangle extends beyond the screen is never drawn.
* §10 — After a failed battle set-up the map is shown without reloading the map graphics, under the background's palette if it was loaded.

### 04 — Maps, characters and movement (`04-map-movement.md`)

* Normal maps map cells left of/above the camera to large screen columns/rows; column 0 can map to screen
  column 20 when the camera is at the right edge (§4.2).
* The alight occupant query applies the looping "−1" screen-cell rule on all map types (§4.2, §12.4).
* A boarding hero (move type 6) clears its grid entry instead of registering (§5.2).
* Party characters write themselves into the grid on every advanced sub-step, even with display 0x1B (§5.3).
* The facing override is reset only for visible sprite characters; facing follows the drawn direction at
  sub-step 3, so spinning characters change their facing and an override drawn at sub-step 3 sticks (§6.3).
* Animation types 0x17/0x18 show the stepping column when the direction comes from the facing override or a
  turn motion (§6.3).
* Rows of maps with H < 17 can be processed twice per frame: double animation speed, double R use for
  animation type 0x15 (§6.6).
* Animation type 0x15 consumes R on every frame change of every processed character (§6.2).
* Map-edge steps neither move nor turn (§7.3).
* Blocked steps turn the character toward the obstacle (except wagons/sleds and animation types 0x16/0x18)
  (§7.3).
* Wagons/sleds are stopped by any layer-1 sprite at the destination regardless of their own layer (§7.3).
* Wandering events never step outside the screen window; route events do but without collisions (§7.3).
* Events bounce at the half-step; route events wait at sub-step 1 instead (§7.4).
* Sub-step stall of characters completing a step on unvisited rows (§7.4).
* Mirrored route playback turns wait (17) into "turn left" (7) (§8.3).
* Movement-control mode 1 does not stop trigger cooldowns (§8.1, §9.7).
* Mouse action paths ignore the movement-control mode, routes and the arrival flag; auto-walk ignores
  the mode; a teleport does not end auto-walk, so the hero keeps turning right (§9.2, §11.1).
* The action button ignores the trigger type; the counter test wraps map coordinates on normal maps (§9.3).
* Step-on ignores the trigger cooldown (§9.4).
* The face-each-other override lasts one draw and does not change facings (§9.5).
* An interim redraw does nothing while the hero's sub-step is 4, and one between the tick render and the
  step-completion step cancels the step-completion processing (§10.4).
* Vehicle update/autorun stops at the first slot on another map (§12.1).
* A pass-abort flag set by an autorun vehicle only ends the vehicle autorun loop; the arrival flag is cleared and
  step completion still runs (§9.1 step 10).
* An escape procedure used in battle teleports at the battle end without setting the pass-abort flag (§1.4).
* Vehicle pages are re-evaluated only while the current map has at least one ordinary map event (§12.1).
* While boarding, slot 0 has id 0: id scans of the vehicle table stop there (§12.1).
* The oldest vehicle is deleted when an 11th distinct vehicle is boarded (§12.2).
* The leader's first step out of a vehicle is not tested at all (§12.4).
* The poison flash redraw advances animations (§13.1).
* An empty party counts as down (§13.2).
* The encounter step counter counts while flying; the first step in a new area never encounters (§13.3).

### 05 — Events (`05-events.md`)

* §3.1 — an event whose file cannot be read is erased from the map tile for the rest of the session.
* §3.3 — vehicle updates and autorun checks stop at the first vehicle entry that is on another map.
* §4.1 — pages 1..3 without condition flags are never chosen; page 0 never checks its conditions.
* §4.1 — if the selected page is the current page, nothing (graphic, route, cooldown) is re-applied.
* §4.3 — items equipped by party members count for item conditions (not for event 99999's first page choice).
* §4.3 — page changes from commands take effect only after the event, but item use-up and battle drops refresh immediately.
* §5.2 — the action button ignores the trigger type; autorun pages can be restarted with it.
* §5.3/§5.4 — bumping needs cooldown exactly 0, stepping on ignores the cooldown.
* §5.7 — an autorun event runs once per page activation; it starts in the second tick after map arrival.
* §5.7 — a teleporting event does not get its cooldown set; a same-map teleport makes an autorun event run again.
* §6.3 — after a teleport (successful or not) the running event has no object; codes 35/37/42/43/44 then do nothing, and 47 does nothing but still suspends.
* §2 — a failed run read leaves the strings already read stored (see `02-data-formats.md` §4.4).
* §3.3/§4.3 — vehicle pages are re-evaluated only while the current map's event list is not empty.
* §4.3/§6.5 — after a failed code 48 the hero-wait record keeps its object, which is then excluded from page re-evaluation.
* §5.5 — an autorun event on a map row not visited by the draw keeps its stale on-screen flag and can autorun far off screen.
* §5.5 — a pass-abort flag set by an autorun vehicle only ends the vehicle loop; step completion still runs.
* §5.2 — the mouse equivalents of the action button skip the movement-control, route, arrival-flag and latch tests.
* §8.2 code 2 — a selection made during the first 200 ms of a choice is discarded.
* §6.4 — the end of every event shows the map, removing pictures and an erased screen (unless erased with a route pending).
* §6.5 — 33 directly followed by 35 (or vice versa) suspends only at the second command.
* §6.5 — a 47 that does nothing still suspends the event.
* §6.5 — a second hero-waiting event overwrites the first one's resumption; a re-triggered waiting event restarts from the top.
* §6.5 — resumption ignores the event's current page conditions.
* §6.6 — the start event always runs page 0; the walking music is started after it.
* §6.7 — the game-over event runs again every loop pass while the party stays defeated.
* §8.2 code 1 — `$W` overrides the "no wait before a choice" rule.
* §8.2 code 2 — a choice cannot be cancelled; label 0 skips the whole choice block.
* §8.2 code 4 — the troop range draws a random number even when max < min (except max = min − 1, which draws nothing).
* §8.2 code 11 — changing experience never changes the level (no level-up, no message).
* §8.2 code 15 — setting an already-ON switch counts as a change unless its number is a multiple of 32.
* §8.2 code 16 — HP < 1 after the change clears the status, also for members already at 0 HP.
* §8.2 code 19 — lowering the level keeps the experience; max HP/MP are re-rolled only if vitality/intelligence changed.
* §8.2 code 23 — followers' destination x is set to the old leader's y.
* §8.2 code 24 — no lower clamp on recovery.
* §8.2 code 41 — relative hero speed change modifies the event's speed instead.
* §8.2 code 54 — removing vehicles also sets the escape destination to a1.
* §7.3 — a single-actor target need not be in the party; non-members' records are changed too.
* §6.5 — a 48 executed while not riding still suspends the event.

### 06 — Windows, fonts, text, messages and input in windows (`06-windows-text.md`)

* Width rounding always adds at least one pixel and up to 32: an exact multiple of 32 grows by 32 (§6.2).
* `Message` reuses an existing message window at its current position, ignoring the requested position (§9).
* Paged-list name labels overlap the value labels; long names are clipped at x = 147 and overdrawn (§5.2).
* List and HP/MP rectangles may extend past the client edge and are clipped (§5.2, §5.6).
* The choice width is computed from the option lengths before `$` expansion (§10.2).
* A choice made in the first 200 ms of a choice window is discarded (§10.2).
* Esc and right click cannot cancel an event choice (§10.2).
* `&` in any window label is a mnemonic prefix: it is hidden and underlines the next character; `&&` shows `&`; width counts include the `&` (§3.3).
* Window darkening for 0.7 / 0.6 follows the integer rules of `03-display.md` §9, not plain truncated products (§4.2).
* `Message` waits: for factor 0.7 the wait is one tenth shorter when the message speed is a multiple of 10 (§9).
* A key given to an inactive focused window is ignored but re-activates it (§8.3).
* Enter/Space never close a lingering message window; other keys do (§8.3).
* Enter in a display-only window is reported as a mouse selection and stores the pointer position (§8.4).
* A right click on an active window cancels the focused window, not the clicked one (§8.7).
* Mouse input over an inactive window goes to the game screen: a left click on it closes the field menu (§8.7).
* The pop-up is placed at the selection mouse y without the +10 used for the keyboard case (§11).
* The version box shows `Runtime Routine  ver 1.23` with two spaces (§12).
* Clicks with client y ≥ 180 never select (§8.7).
* The transparent cursor redraws the value label for slots 4, 5, 8 but not 7 (§7.3).
* The transparent background is a one-time snapshot including the windows below; it does not follow later scene changes (§4.2).
* Page-indicator total is re-parsed from its own label text on every page change (§8.6).
* The cursor starts on row 0 even if that row is disabled, and may rest on disabled or empty rows (§7.1, §7.4).

### 07 — Field menu, sub-menus, item and magic use outside battle, shops and inns (`07-menus-shop-inn.md`)

* Items chosen through the target list are used by party member 0 even if that member is dead (§2.2).
* After a caster m ≠ 0 was chosen in the magic sub-menu, items used through the target or destination list are taken from member m's action record and used by member m (§2.2, §3.5).
* The field menu writes battle action records: command of position 0 on every main-menu selection, item/target/spell fields on choices, −1 on cancels (§1.7).
* A left click on an inactive window closes the whole field menu (§1.6).
* Heal-all and escape cast as spells close the menu before casting, so each message of the cast gets its own window (§3.3).
* Teleport and escape spells skip the animation sound but still apply its palette flash (§3.4).
* The status screen shows 気絶 (strings[26]) for the knocked-out bit 0x20 even at HP ≥ 1 (§5).
* In a vehicle, the party order still gives the new leader's character the previous leader's move type and speed (§6).
* The shop command width ignores strings[101] unless a3 = 0 (§8).
* Shop and inn message lines are picked without an end-of-string check (§8, §9).
* Buff spells (types 17–19) cast by items are wasted in the field, while the same spells work when cast as spells (§2.3, §3.4).
* Heal-all in the field heals only the first four members (§3.4).
* Reserve members are never disabled in the caster list, even when dead (§3.1).
* Enter/Space on an already-placed member in the party order undoes the last placement (§6).
* Party order keeps each character's facing; only position, target, sub-step and timer move (§6).
* Equipment change recomputes attack/defence and discards temporary battle buffs (§4).
* The equipment "down" text has a half-width space before ポイント (§4).
* Status screen shows the remaining experience even at level 100 (§5).
* The shop's empty-buy-list pop-up uses strings[106] 売るアイテムはありません (§8).
* Shop list pages reached by page change show full prices even when selling (§8).
* Buy and sell prompts use full-width vs. half-width parentheses respectively (§8).
* Shop greeting is repeated after cancelling a list if no deal was made (§8).
* The inn's "restore HP" flag revives dead members without clearing their condition word (§9).
* The inn fade-in resets any event screen tint (§9).
* Inn status masks also clear bit 0x20 and the upper byte of the condition word (§9).
* System menu 終了する quits without confirmation (§7).
* The menu cannot be opened by Esc while the leader is between tiles (§1.1).

### 08 — Battles (`08-battle.md`)

* 0.3 — The 0.7 message wait is one tenth shorter than 7 × speed / 10 when the message speed is a multiple of 10 (extended-precision product, truncated).
* 1.1 — The troop draw of event command 4 with to < from − 1 takes a random number and uses its remainder by a negative divisor; with to = from − 1 it takes none.
* 0.5 — A monster whose rectangle is not completely inside the screen is not drawn at all (`03-display.md` §1.2, §10).
* 1.9 — A failed battle setup (missing bgNN.bmp or MONSTPIC.DAT) leaves the battle BGM playing on the map, does not reload the map graphics and may leave the background palette on screen.
* 7 — The battle end unloads every loaded wave sound except the last one in the list (`09-audio-movies.md` §4.3).
* 2.1 — Back in the command menu does not update the enabled state of 魔法 for the newly selected member.
* 2.5 — The y position of the party target window for magic uses the stale party-list count (living members at the last action).
* 2.11 — Auto battle's lowest-HP choice ignores enemies with HP > 9999 and then uses rand(n) as a slot number.
* 2.11 — While auto battle is on, the heal override also replaces commands chosen manually in the same turn.
* 3.1 — An enemy killed last by end-of-turn poison ends the battle without any victory processing or rewards (if someone can input).
* 3.1 — A party wiped out by end-of-turn poison triggers one more turn-order roll and the first combatant's turn before defeat.
* 3.3 — Members who could not input execute their stale action record (from earlier turns, battles or the field menu) if cured before acting.
* 3.3 — A confused member whose command became attack still gets its spell field overwritten (undefined value; use −1).
* 3.3 — Retargeting picks the living member of the target side with the largest HP deficit, first on ties, not a random one.
* 3.4 — The escape chance divides by the highest enemy agility (division by zero possible).
* 3.6 — The second hit of a double attack on a party member is skipped if the monster database record with that number has max HP ≤ 0.
* 3.7 — Enemies killed by poison still roll for waking up and may show the waking message.
* 3.8 — All living members paralysed counts as defeat, even at battle start.
* 4.1 — Attacking a dead target shows only the target's name.
* 4.3 — Magic defence reduces the rate of single-target spells only for spell numbers 0..10 (not types), capped at 200.
* 4.4 — Damage-all spells subtract magic defence for every spell number, without the 200 cap.
* 4.4 — The faint spell (type 7) and enemy fleeing give no experience, gold or drop.
* 4.4 — Revive can set HP to 0 while reporting success.
* 4.4 — Heal-all always applies the random spread (even at rate 0) and prints nothing for members already at full HP.
* 4.4 — A confused caster's teleport uses the target value as destination map id.
* 4.7 — Stat items used in battle lose their attack/defence/agility part at victory; their messages always say 上がった.
* 4.8 — The paralysis cure line doubles "は" with the default strings.
* 5.4 — A negative MAGIC.DAT type is read as unsigned 16-bit and gives an animation number above 99.
* 5.5 — Anm files narrower than 128 pixels (0 frames) are still prepared and played frame by frame, with all random numbers and delays, but draw nothing and play no sound.
* 5.5 — The random start frames are drawn also for sub-animations of dead or absent enemies.
* 5.7 — Every kill dissolve takes one random number per 2 × 2 block of the monster picture.
* 6.1 — The "生き残ったﾒﾝﾊﾞｰ…" victory text is unreachable.
* 6.2 — Experience is not divided; reserve members get half, dead members nothing.
* 6.3 — In-battle changes of attack/defence/agility persist after escaping, teleporting or losing.
* 1.6 / 5.2 — The colour key of monster and animation pictures is the top-left pixel's palette index.

### 09 — Audio and movies (`09-audio-movies.md`)

* §2 — base names are cut at the last 0x5C byte even if it is a Shift_JIS trail byte.
* §3.1 — the device is chosen by a case-sensitive search for `.mid` / `.wav` in the name; anything else
  (including `.MID`, `.WAV`, `NOSOUND.WAV`) is taken for a CD track.
* §3.1 — the old music is stopped before the new one is checked; a missing file leaves silence.
* §3.1 — a failed start still sets the current BGM name, so the same name is not retried unless forced.
* §3.1 — the type bits of b select only the volume control, not the device.
* §3.6 — the screen-mode switch and loading a save restart the music (force); other resumes restart it only
  because `演奏停止` was requested before.
* §1.2 — the built-in default slot names are never used; slots start without a name (slot 11 stays so in pre-0.96 games).
* §3.6 — a failed battle set-up leaves the battle music playing on the map and does not resume the saved BGM.
* §4.1 — a MIDI jingle's volume goes to the MIDI or CD control by its type bits (type 3 changes the CD volume).
* §4.2 — the WAV jingle wait tests the sounds numbered 1..3, not the jingle's channel; it ends at once when other loaded sounds hold all three numbers.
* §4.2 — the jingle skip tests whether Enter/Space is held, not pressed.
* §4.2 — a MIDI jingle that exists but cannot be played makes the wait last until Space/Enter is held.
* §4.3 — finished sound effects stay loaded and keep their sound numbers until the next battle end, movie or screen-mode switch.
* §4.3 — the battle end leaves the last loaded wave sound loaded (stopped), keeping its number.
* §4.3 — when all sound numbers 1..255 are in use, a new sound plays but can never be stopped.
* §5.1 — MIDI/CD volume is a persistent output-wide setting; a sound effect with type 2/3 changes the MIDI/CD
  volume and itself plays at full volume.
* §5.1 — frequency is applied to wave sounds regardless of the type bits.
* §5.2 — when all three effect channels are busy, channel 1 is always the one replaced.
* §7.3 — fitted movies are clamped to 639 × 479 after positioning.
* §7.4 — an unopenable movie leaves an empty area until confirm.

### 10 — Save files (`10-save-files.md`)

* §2.2 — only the first 10 bytes of the identification are compared; the version text is never checked.
* §2.2 — the text-line reader drops the last byte read, assuming it is the LF; names of 255 bytes or more
  desynchronise the rest of the file.
* §2.2, §5.2 — vehicle objects are written by object marker but read by event id.
* §2.3 — when riding, only party slot 0 is re-linked by the original.
* §2.3 — learned magic is overwritten from the database except event-taught spells with database level 0.
* §2.4 — loading from the menu discards the saved palette (CHARA.BMP reloaded with the stored tint); loading from the title uses it.
* §1.3 — money is stored twice; the second copy wins.
* §1.4 — the current BGM's a, b are not updated by `演奏停止`.
* §1.4 — a slot without a name is written as the text `(null)`.
* §2.3, §4 — the saving-enabled setting and the battle action records keep their values across a load.
* §2.4 — loading from the title shows the saved palette as it is; the stored tint is not re-applied until the next palette reload.
* §3.1 — event command 52 saves even when saving is disabled.

## E. Verification plan

The implementation team can check its work against the following observable facts, using the
sample games (any RPG Maker 95 game data) without the original runtime.

1. **Random numbers.** Starting from the initial state, the first ten outputs of the generator are
   5540, 221838220, 695693412, 1410949376, 822182988, 1990930872, 236698092, 2108515212,
   1428137313, 1164941360 (`01-runtime.md`, Random numbers).
2. **File parsing.** All maps, events, databases and monster pictures of every sample game load
   without error; every event file either parses completely or is rejected by the rules of
   `02-data-formats.md`. The sample save files load, and saving immediately after loading writes a
   file of the same size whose meaningful fields are identical (`10-save-files.md`).
3. **Layouts.** Window rectangles and label positions match the tables of `06-windows-text.md` and
   `07-menus-shop-inn.md` pixel for pixel when the font metrics given there are used. Use the game's
   own `strings.dat` when checking widths (most games differ from the defaults of §A).
4. **Determinism.** With the same input timing, battles started from the same state give the same
   damage numbers and messages every time. Example (game パンダとウサギ, its first battle troop, a
   frog with attack 19 against a party member with defence 31): the base damage is 19 − 31/2 = 4,
   the spread is ±1, and the original showed 5; a party member with attack 21 against the frog's
   defence 15 has base damage 14 and the original showed 15 (`08-battle.md`, physical attack). With
   the save file `testdata/panda_original_runtime.sav` (in the delivery package, in the folder
   `testdata` next to the spec folder; written by the original runtime for that game), loading it at the title screen and starting the
   battle immediately, the original's first two damage messages were 33 (to the frog, by the third
   member) and 3 (to the first member); an implementation with the documented random-number order
   reproduces exactly these numbers.
5. **Timing.** A map tick happens at most every 21 ms; a character with speed 2 needs the number of
   ticks per tile given in `04-map-movement.md`; messages wait message speed × factor tenths with the
   exact rule of `06-windows-text.md` §9 (for factor 0.7 and message speed 30 the wait is 20 tenths,
   not 21).
6. **Transitions.** Each transition type draws the blocks in the order and with the delays given in
   `03-display.md`; with a fixed random state, randomized transitions give identical frames.
7. **Robustness.** Driving each sample game with random key presses for several minutes must not
   crash or hang; the game-over event, shops, inns, battles, menus and save/load are reached in
   most games.

Where a behaviour cannot be verified without the original, the specification team can run
comparison tests on request and report the observed results in writing.
