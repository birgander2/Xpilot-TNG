# XPilot TNG – Game Modes Overview

In XPilot TNG, game modes are defined through map properties (`.xp` / `.xp2` map files), server configuration (`lib/defaults.txt`), or command-line arguments passed to the server binary. Multiple modes and rules can also be combined.

---

## Maps in `lib/maps`

| Map | Type | Fits |
|---|---|---|
| `ndh.xp2`, `killing.xp2`, `arena2.xp2`, `gadget.xp2`, `hive.xp2`, `reactor.xp2`, `destiny2.xp2` | Dogfight / deathmatch | FFA, Team Deathmatch, Tag, Survival, Timed |
| `polybloods.xp2`, `bloods-smartbots.xp2` | Two predefined teams, one treasure ball per team | Capture the Flag only |
| `tourmination.xp2` | Dogfight, 12 bases, `tagGame` on by default | Tag, FFA, Team Deathmatch |
| `pacman.xp2` | Team map (two teams) with many targets and cannons | Special map, see its own rules |
| `grandprix.xp2`, `zandvoort.xp2`, `donkeyko.xp2`, `donkeyko2.xp2` | Race tracks with checkpoints | Race |
| `spaceball.xp2`, `fb.xp2` | Ball games with goals | Spaceball / football |

Not covered by the current collection: Target Mode, Ballrace and Dodgers. Each of these sections says what a map would need.

---

## 1. Deathmatch / Free-for-All (FFA)
*Classic dogfight where every pilot fights for themselves.*

- **Objective:** Collect items (missiles, mines, lasers, cloaks, ECM, etc.) and eliminate other ships to score points.
- **Rules:** Infinite lives (`limitedLives: no`), instant respawn at home bases.
- **Suitable Maps:** `ndh.xp2`, `killing.xp2`, `arena2.xp2`, `gadget.xp2`, `hive.xp2`, `reactor.xp2`, `destiny2.xp2`
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map ndh.xp2
  ```

---

## 2. Teamplay / Team Deathmatch
*Combat divided into two teams.*

- **Objective:** Wipe out the enemy team and accumulate team score.
- **Rules:** Every player belongs to a team and spawns at that team's bases. With `teamImmunity` enabled, shots and mines of teammates do no harm.
- **Suitable Maps:** Practically every dogfight map above (`ndh`, `killing`, `arena2`, `gadget`, `hive`, `reactor`, `destiny2`, `tourmination`). `-teamPlay true` randomly splits the bases into two teams at map load. No map in the collection comes with deathmatch teams predefined; `polybloods` and `bloods-smartbots` are treasure maps (see Capture the Flag).
- **Not for:** Race maps – teamplay is ignored when `timing` is on.
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map ndh.xp2 -teamPlay true
  ```

---

## 3. Capture the Flag (CTF) / Treasure Mode
*Objective-based treasure heist with tractor beams and tethered balls.*

- **Objective:** Snag the enemy team's treasure ball with a tractor beam or connector cable, tow it across the arena, and deposit ("cash in") at your own base.
- **Rules:** When `captureTheFlag` is active, you cannot cash in an enemy ball unless your own treasure is safely in your base. With `treasureKillTeam`, a team is eliminated if all their treasures are lost.
- **Suitable Maps:** `polybloods.xp2` and `bloods-smartbots.xp2` (one treasure per team, teamplay on). Any other team map needs one treasure (ball) per team next to that team's bases.
- **Ballrun records:** the server keeps a top ten list of ballruns (the time from lifting a ball out of its treasure until it is cashed), human players only, the best time of each nick, per map (treasures) and game speed in `$XDG_DATA_HOME/xpilot-tng/balltimes/`. When a round brought a new entry, TNG clients show the list in a popup at the end of the round (new entries marked); Esc, Enter, Space or a click closes it, otherwise after 30 seconds. The next round starts when every player with a TNG client has closed it (at most 35 seconds); until then all ships wait at their bases.
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map polybloods.xp2 -captureTheFlag true
  ```

---

## 4. Target Mode / Base Destruction
*Base assault and defense mode used in competitive tournaments.*

- **Objective:** Defend your team's stationary targets while destroying all enemy targets using cannons, torpedoes, or nukes.
- **Rules:** When `targetKillTeam` is enabled, the entire team dies and loses the round as soon as their last target explodes.
- **Suitable Maps:** None in the current collection. A map needs targets assigned to each team. (`pacman.xp2` and the Donkey Kong maps contain targets, but they serve other purposes there.)
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map <target-map> -teamPlay true -targetKillTeam true
  ```

---

## 5. Race Mode
*High-speed maneuvering race through checkpoints against the clock.*

- **Objective:** Fly through a sequence of numbered checkpoint gates in order to complete laps in the shortest time.
- **Rules:** Timing engine tracks lap times, lap counts (`timing_loops`), and checkpoint splits. Weapons are usually disabled.
- **Suitable Maps:** `grandprix.xp2`, `zandvoort.xp2`, `donkeyko.xp2`, `donkeyko2.xp2` (all have `timing` on already).
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map grandprix.xp2
  ```
- **End of a race:** after the first player finished, the race goes on for `raceFinishTime` seconds (default 60) so the others can finish too; it ends earlier when nobody races any more. `raceFinishTime 0` keeps the old rules (the race ends with the first finish, with limited lives when at most one player is still racing).
- **Results and records:** when a race ends, TNG clients show a popup with your total time and best lap, the standings of the race and two top ten lists: whole races and single laps. The lists hold human players only, the best time of each nick, and are kept by the server per course (map and checkpoints), number of laps and game speed in `$XDG_DATA_HOME/xpilot-tng/racetimes/` (default `~/.local/share/xpilot-tng/racetimes/`), one text file per combination. Esc, Enter, Space or a click closes the popup; it closes by itself after 30 seconds. The next race starts when every player with a TNG client has closed it (at most 35 seconds); until then all ships wait at their bases.

## 6. Ballrace
*Precision obstacle race towing an elastic physics ball.*

- **Objective:** Same as regular race mode, but you must tow a heavy physics ball on an elastic tether through every checkpoint.
- **Rules:** High inertia and tether elasticity (`ballConnectorSpringConstant`, `ballConnectorDamping`) make turns and wall avoidance challenging.
- **Suitable Maps:** None in the current collection. A map needs checkpoints plus a ball, with tracks wide enough for the tether.
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map <ballrace-map> -timing true -ballrace true -ballraceConnected true
  ```

---

## 7. Spaceball
*Zero-gravity space soccer / hockey.*

- **Objective:** Maneuver a special neutral ball into the opponent's goal/base using towing cables, tractor beams, or weapon blasts.
- **Rules:** Two teams, limited lives by default, bouncing off walls, and designated scoring areas.
- **Suitable Maps:** `spaceball.xp2`, `fb.xp2` (*Ultimate Football*)
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map spaceball.xp2
  ```

---

## 8. Tag / "It" Mode
*One hunted player against the rest of the server.*

- **Objective:** One player is designated as "It". Hunting and killing "It" earns large point bonuses and transfers "It" status to the killer.
- **Rules:** Killing "It" applies `tagKillItScoreMult` (default 10x). If "It" kills a pursuer, they get `tagItKillScoreMult` (default 2x).
- **Suitable Maps:** Any dogfight map. `tourmination.xp2` has `tagGame` on by default.
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map tourmination.xp2
  # Or on any other dogfight map:
  ./xpilot-tng-server -map ndh.xp2 -tagGame true
  ```

---

## 9. Survival / Limited Lives (Last Man Standing)
*Elimination tournament where every life counts.*

- **Objective:** Be the last surviving pilot or team remaining in the arena.
- **Rules:** Each player begins with a fixed pool of lives. When lives reach 0, the player is permanently eliminated until the match resets.
- **Suitable Maps:** Any dogfight map with `-limitedLives true`.
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map killing.xp2 -limitedLives true -worldLives 3
  ```

---

## 10. Dodgers / PvE Survival
*Evasion challenges against automated turrets and hostile AI.*

- **Objective:** Survive as long as possible while dodging flak cannons, moving hazards, and aggressive smartbots without getting hit.
- **Rules:** Tests pure piloting reflexes, throttle control, and fuel conservation; scoring is tied to survival time (`survivalScore: 1`).
- **Suitable Maps:** None in the current collection. A map needs cannons as the main hazard. (`gadget.xp2` and `ndh.xp2` have a few cannons, but are dogfight maps.)
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map <dodgers-map> -survivalScore 1
  ```

---

## 11. Timed Match ("Pizza Mode")
*Match with an automated hard time limit.*

- **Objective:** Play any game mode (FFA, Team, CTF) with a countdown timer (e.g. 15 minutes until the pizza arrives).
- **Rules:** When `gameDuration` runs out, the server stops the match and crowns the player/team with the highest score.
- **Suitable Maps:** Any map and mode (best on dogfight and ball maps; race maps have their own end).
- **How to Start:** Set the duration in minutes via `-gameDuration` (or `-time`):
  ```bash
  ./xpilot-tng-server -map killing.xp2 -teamPlay true -gameDuration 15
  ```
