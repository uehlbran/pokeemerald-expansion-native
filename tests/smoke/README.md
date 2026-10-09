# Smoke test: the opening, replayed

Gate (d). `opening.replay` drives the native build, headless and
deterministic, from power-on through the opening of Pokemon Emerald and back
from a cold restart: title, New Game, Professor Birch's speech, the truck,
Littleroot Town, the clock, the neighbour, the north exit, Route 101, the
first battle (Treecko against Poochyena), Birch's lab, Save, `restart`,
Continue. It checks game state after every step with `expect` and
`wait-until`, and writes a dump (`key=value` lines) and often a screenshot
after each.

## Running

```
make -f Makefile.native -j"$(nproc)" native BITS=32                 # build/native32/pokeemerald-native
tests/smoke/run.sh build/native32/pokeemerald-native out/            # exit 0 if every dump and screenshot matches

make -f Makefile.native -j"$(nproc)" native BITS=32 SANITIZE=1      # build/native32-san/pokeemerald-native
ASAN_OPTIONS=halt_on_error=0 tests/smoke/run.sh build/native32-san/pokeemerald-native out-san/
```

`run.sh` builds nothing. It runs the replay with `--headless --silent
--unthrottled --rtc-fixed 1700000000` and a save file and RTC offset file in
`OUTDIR/work/`, then compares:

- every dump with `expected/<name>.txt`, fact by fact; the `frame` fact is
  left out (it counts this build's timing, which any change to the engine
  shifts, and nothing depends on it);
- every screenshot with `expected/screenshots.md5` (the PPMs stay in
  `OUTDIR/work/`; compare two by eye after converting to PNG);
- the save round trip: `player_name`, `map`, `party0_species` and
  `party_count` in `25_saved.txt` (dumped just after the save) against the
  same four in `27_continued.txt` (after the restart and Continue);
- the game's stderr for sanitizer reports, which fail the run unless a line
  of `sanitizer-known.txt` (if there is one) matches.

The plain and the sanitized build must produce the same dumps and the same
screenshots: the expectations are shared. A difference between them is a
bug (undefined behaviour that the two builds compile differently).

Takes about 30 seconds plain and a minute sanitized.

## Updating the expectations

When the game's behaviour changes on purpose (a new expansion, a fix that
changes what is drawn), run the plain build once with `--update`, read the
diff of `expected/` in git, and commit it:

```
tests/smoke/run.sh build/native32/pokeemerald-native out/ --update
git diff tests/smoke/expected/
```

`--update` refuses to write from a run that failed. Then run the sanitized
build without `--update`: it must agree.

## The script

The format is documented in `platform/replay.h`; a few things matter when
editing `opening.replay` (its first nine steps are `opening_start.replay`, included):

- `wait-until FACT=VALUE max N` waits for a fact of the game
  (`platform/inspect.h` lists them: `callback2`, `task_first`, `task_last`,
  `tasks`, `map_name`, `pos`, `in_battle`, `controls_locked`, ...). The
  script uses no fixed frame counts except `wait N` after a state change that
  the facts do not show: a key pressed in the frame a screen becomes ready is
  lost (the naming screen after its fade, a map's arrival script that locks
  the controls a few frames after the map is up, a turn on the spot before an
  `A`). Those waits are commented.
- `mash KEYS until FACT=VALUE max N` presses over and over, for dialogue
  whose number of pages the script should not count. Text prints faster with A
  held, so a mash holds the key for 4 frames of every 8. It stops when the fact
  holds and leaves the keys up for one frame, so that the next `press` is new.
  Do not mash through a yes/no question whose answer you care about: A is
  Yes. The script mashes up to the question (`task_last=Task_HandleYesNoInput`)
  and answers it.
- Walking: `hold DIR`, `wait-until pos=X,Y`, `release DIR`. The map
  position is updated when a step starts, so the step in progress finishes
  after the release.
- `restart` ends the process (shutting every module down, which flushes the
  save) and starts the same executable again with the same arguments plus
  `--resume-at LINE`, so the script goes on after the `restart` line. Why a
  re-exec and not the in-game soft reset: a soft reset (A+B+Start+Select)
  only reinitialises RAM, so it cannot show that the game starts from the save
  file alone, which is what a player's switch-off and switch-on does.
  `--rtc-fixed` is advanced by the time that has passed, so the clock does not
  run backwards across the restart. After a restart the frame counter starts
  from 0 again, so a `frame N` after `restart` is a parse error (use `wait`).
  `--rtc-fixed` also makes the clock start at UTC with no host time zone
  (run.sh sets `TZ=UTC` too). Every `wait N` in the script has its reason in a comment.

## Before the save blocks exist

`opening_start.replay` begins with four `expect` lines at frame 1, before the game has set up its
save blocks: `flag:2144=0`, `var:16384=0`, `player_name=none` and `map=none`. They fail if a fact
that reads a save block dereferences NULL (`inspect.c`, `read_fact`) or gives a different pre-boot
value. Both `run.sh` and `run_debug_menu.sh` run them, as the first lines of the script they include.

## The key sequence

The player is the boy, `Ab`. The names are `opening.replay`'s dump names.

| Step | Keys | Waits for |
|---|---|---|
| `01_title` | none | the intro, then the title screen's "press start" phase (`Task_TitleScreenPhase3`) |
| `02_main_menu` | START | the main menu (a fresh save has New Game and Options) |
| `03_gender` | A (New Game); A mashed through Birch's speech | the gender question; then A (Boy) and A mashed to "what is your name" |
| `04_naming` | A to open the naming screen; on it: A, RIGHT, A, START, A | the screen's fade-in; the keyboard swap after the first letter; the name `Ab` |
| `05_truck` | A mashed through "So you are Ab?" | `CB2_NewGame`, then the overworld in `MAP_INSIDE_OF_TRUCK` |
| (truck) | RIGHT | the controls, then `MAP_LITTLEROOT_TOWN` |
| `06_house` | A mashed through Mom's arrival scene | `MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F` and its scripts |
| `07_clock_set` | UP to the stairs, LEFT to (5,2), UP, A; on the clock: A, then UP (Yes), A | `CB2_WallClock`, the confirmation, Mom's visit |
| `08_mom_tv`, `09_outside` | RIGHT, DOWN | Mom's TV scene; `MAP_LITTLEROOT_TOWN` again |
| `10_may_2f`, `11_met_may` | RIGHT to the neighbour's door (14,9), UP; upstairs, DOWN, RIGHT, RIGHT again, A on the Poke Ball at (5,4) | the scenes; `town_state=1` |
| `13_outside_again`, `14_warned` | down and out, LEFT to x=11, UP to the north exit | the little girl's warning, `town_state=2` |
| `15_route101`, `16_birch_chase` | UP onto Route 101 | Birch's "Help me" scene |
| `17_starter_screen`.. (`19b_battle` is mid-battle) | LEFT to (7,15), UP, A on the bag; on the screen LEFT (Treecko), A, A (Yes) | `CB2_StarterChoose`, the confirmation, `in_battle=1`, Treecko (species 252) in the party |
| `20_battle_end` | A mashed (FIGHT, first move, ...) | `in_battle=0` |
| `21_lab` .. `23_lab_free` | A mashed through Birch's thanks; B at "give a nickname?" (No); A at "go see your rival?" (Yes) | the lab, both questions (`task_last=Task_HandleYesNoInput`), the controls |
| `24_start_menu`, `25_saved` | START, DOWN x3 (SAVE), A, A mashed through the prompts | `task_last=Task_ShowStartMenu`; the controls again; `save_counter=1`, `flash_dirty_writes` > 0 |
| `restart` | | a new process |
| `26_continue_menu`, `27_continued` | START, A (Continue is first) | the title, the main menu, `CB2_Overworld`; the same name, map and Pokemon |

## Why not the title screen's "SEL: New Game"

`src/quickstart.c` starts a new game from the title screen with SELECT,
skipping the speech, the naming screen and the truck, with a random gender and
a default name. The gate goes through the normal menu path instead, so that
those screens run, which is where the platform's bugs were.

# Gate (e): the debug menu, driven natively

`debug_menu.replay` plays the first steps of the opening (the shared
`opening_start.replay`, included with `include`), stands in Littleroot Town and
drives expansion's own debug menu (`DEBUG_OVERWORLD_MENU`, hold R and press
Start) exactly as a player would: list menus with A and B, numeric entries with
the D-pad (UP and DOWN change the digit under the cursor by its place value,
LEFT and RIGHT move to the next digit). It uses no cheat of ours.

```
tests/smoke/run_debug_menu.sh build/native32/pokeemerald-native out/            # exit 0 if every dump and screenshot matches
ASAN_OPTIONS=halt_on_error=0 tests/smoke/run_debug_menu.sh build/native32-san/pokeemerald-native-san out-san/
tests/smoke/run_debug_menu.sh build/native32/pokeemerald-native out/ --update   # after a deliberate change
```

It shares `lib.sh` with `run.sh`: the expectations are in
`expected_debug_menu/` (dumps compared minus `frame`, screenshot md5s), sanitizer
reports in stderr fail it, and what was saved (`14_saved`) must be what Continue
loads after the restart (`15_continued`: name, map, party, PC box; the replay also
expects the lead's extras and moves before the save and after Continue). 28
seconds plain (measured on 32 cores, 2026-10-08). Both builds must produce the same dumps and screenshots.

What it does, in order:

| Step | What | Checked by |
|---|---|---|
| Flags & Vars | sets flag 2144 (`FLAG_SYS_POKEMON_GET`, which puts POKeMON on the Start menu), sets var 16384 (`VAR_TEMP_0`) to 7, toggles all badges (so the level-100 lead obeys) | `flag:ID`, `var:ID` |
| Give item | Charizardite X 293 (Mega Stone), Normalium Z 357 (Z-Crystal), Grass Tera Shard 783, Ability Capsule 79, Dynamax Candy 108 x3, Z-Power Ring 704 | `bag_item_count:ID`, the bag screenshots |
| Give Pokemon, phase A | Treecko 252 L100 (generation 3), Bulbasaur 1 (1), Chikorita 152 (2), Vulpix-Alola 961, Meowth-Galar 974 (basic give), Tauros-Paldea-Combat 1402 (complex give: shiny, Dynamax level 10, Gigantamax factor, Tera Grass) | `party_count`, `party_species:N`, `party_name:N`, `party_level:N`, `party_extras:N` |
| Party full | Venusaur 3 (complex give, Gigantamax factor) and Charizard-Mega-X 907 go to PC box 1 | `pc_box0_count`, `pc_box_species:0` |
| Display | Start, POKeMON, the summary screen of each member (screenshot each), Start, BAG (items and key items pockets) | `summary_species`, `party_slot` |
| Battle | Trainers > Trainer 1 = 10, Try Battle: a real trainer battle with the party as it is; A through the intro to the player's choice (`battle_main=HandleTurnActionSelectionState`, screenshot: `a_battle`, `b_battle` differ by the lead), then mashed to the end | `in_battle` transitions, no crash |
| Phase B | Party > Clear Party, then Turtwig 387 (complex give: shiny, move 1 Tackle, Dynamax 10, Gmax factor, Tera Grass; `party_extras:0`, `party_move:0:M`), Snivy 495 (5), Chespin 650 (6), Rowlet 722 (7), Grookey 810 (8), Sprigatito 1289 (9); display and battle again | as above |
| Save | Save, `restart`, Continue | the same party, PC box, bag, flag and var |

The numeric entries are checked as they are made: `debug_input` is the number on
the entry's screen, `debug_step` its step, so `expect debug_input=1289` proves
the D-pad sequence and `wait-until debug_step=1.0` that the confirmed number was
taken before the next key is sent.

Two replay commands exist for this script: `include FILE` (the lines of FILE
replace the line, before parsing; line numbers in messages and for `restart` are
those of the expansion, which is written to `OUTDIR/work/expanded.replay`) and
`press KEYS times N`. A failed command also leaves the screen in
`OUTDIR/work/failed.ppm`.

Limitation: a fact that does not exist (or an argument out of range) is found when the
command runs, not when the script is parsed.

The summary screen's own state is static in expansion's code, so
`summary_species` is the party menu's slot at the time the summary was opened
(moving to another Pokemon inside the summary screen is not followed; the script
opens each member from the party menu).

# Gate (h): a save carried between the 32-bit and the 64-bit build

`run_cross_width.sh` plays the first half of the opening on one build and the
second half on the build of the other width, from the save file the first
wrote, as a player moving the cartridge's save between the two would. The
64-bit build lays out `SaveBlock1` differently (two members hold pointers), so
the second build converts the save when it loads it (`platform/save_convert.h`;
`docs/DECISIONS.md`, "Gate (h) is a platform-side save converter").

```
make -f Makefile.native -j"$(nproc)" native BITS=32
make -f Makefile.native -j"$(nproc)" native BITS=64
tests/smoke/run_cross_width.sh build/native32/pokeemerald-native build/native64/pokeemerald-native out/   # the gate
tests/smoke/run_cross_width.sh build/native64/pokeemerald-native build/native32/pokeemerald-native out/   # the way back

make -f Makefile.native -j"$(nproc)" native BITS=64 SANITIZE=1
ASAN_OPTIONS=halt_on_error=0 tests/smoke/run_cross_width.sh build/native32/pokeemerald-native build/native64-san/pokeemerald-native out-san/
```

The arguments are FROM, TO and OUTDIR. Each step runs in its own directory of
`OUTDIR/work/`, with its own copy of the save file:

| Step | Build, script | Checked |
|---|---|---|
| `from/` | FROM, `opening.replay` cut just before its `restart` (a copy in the directory, with the file it includes) | exit 0, no sanitizer report, dumps `01_title` .. `25_saved` and their screenshots as in `expected/`, the save file's header names FROM's width and layout 1 |
| `to/` | TO, `opening.replay` with `--resume-at` the `restart` line and `--rtc-fixed` advanced as `restart` advances it | `26_continue_menu` and `27_continued` and their screenshots as in `expected/`; `player_name`, `map`, `party0_species` and `party_count` of `27_continued` equal to FROM's `25_saved`; stderr says the save was converted; the file is unchanged (a conversion is not a game write) |
| `bare/` | as `to/`, on the save without its 28-byte header; only when FROM is 32-bit | the same, and stderr says the file has no header |
| `again/` | TO, `cross_width_save.replay`: Continue, save, `restart`, Continue | the replay's `expect`s (one save before, two after); the header now names TO's width; `28_saved_again` and `29_continued_again` agree and show the scene of `27_continued` |

Between them FROM and TO must write every dump and screenshot of
`expected/`. The `restart` line is found from the expansion FROM wrote
(`from/expanded.replay`, the cut script with its include expanded, ends just
before it) and checked against the one TO wrote. A bare image is taken to have
the 32-bit layout (R7 of milestone 7: its only producers are GBA emulators and
native builds from before the header), which `bare/` checks.

43 seconds plain from 32 to 64 bits (measured on 32 cores, 2026-10-09).
