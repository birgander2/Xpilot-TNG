# XPilot TNG – Game Modes Overview

In XPilot TNG, game modes are defined through map properties (`.xp` / `.xp2` map files), server configuration (`lib/defaults.txt`), or command-line arguments passed to the server binary. Multiple modes and rules can also be combined.

---

## 1. Deathmatch / Free-for-All (FFA)
*Classic dogfight where every pilot fights for themselves.*

- **Objective:** Collect items (missiles, mines, lasers, cloaks, ECM, etc.) and eliminate other ships to score points.
- **Rules:** Infinite lives (`limitedLives: no`), instant respawn at home bases.
- **Suitable Maps:** `ndh.xp2`, `killing.xp2`
- **How to Start:** Launch the server with any standard deathmatch map:
  ```bash
  ./xpilot-tng-server -map ndh.xp2
  ```

---

## 2. Teamplay / Team Deathmatch
*Cooperative combat divided into colored teams.*

- **Objective:** Coordinate with teammates to wipe out enemy teams and accumulate team score.
- **Rules:** Teams spawn at distinct team bases. Friendly fire is disabled when `teamImmunity` is enabled.
- **Suitable Maps:** `polybloods8T.xp2`, `polybloods9Tb2.xp2`
- **How to Start:** Use a dedicated team map or pass `-teamPlay true`:
  ```bash
  ./xpilot-tng-server -map polybloods8T.xp2
  # Or force teamplay on any map:
  ./xpilot-tng-server -map blood-music.xp2 -teamPlay true
  ```

---

## 3. Capture the Flag (CTF) / Treasure Mode
*Objective-based treasure heist with tractor beams and tethered balls.*

- **Objective:** Snag the enemy team's treasure ball with a tractor beam or connector cable, tow it across the arena, and deposit ("cash in") at your own base.
- **Rules:** When `captureTheFlag` is active, you cannot cash in an enemy ball unless your own treasure is safely in your base. With `treasureKillTeam`, a team is eliminated if all their treasures are lost.
- **Suitable Maps:** Maps with defined treasure objects and team bases (e.g. `teamcup.xp`, dedicated CTF maps).
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map <ctf-map> -teamPlay true -captureTheFlag true
  ```

---

## 4. Target Mode / Base Destruction
*Base assault and defense mode used in competitive tournaments.*

- **Objective:** Defend your team's stationary targets/generators while destroying all enemy targets using cannons, torpedoes, or nukes.
- **Rules:** When `targetKillTeam` is enabled, the entire team dies and loses the round as soon as their last target explodes.
- **Suitable Maps:** `teamcup.xp`, `tourmination.xp2`
- **How to Start:** Launch with a target-enabled tournament map:
  ```bash
  ./xpilot-tng-server -map tourmination.xp2
  # Or Team Cup format:
  ./xpilot-tng-server -map teamcup.xp
  ```

---

## 5. Race Mode
*High-speed maneuvering race through checkpoints against the clock.*

- **Objective:** Fly through a sequence of numbered checkpoint gates in order to complete laps in the shortest time.
- **Rules:** Timing engine tracks lap times, lap counts (`timing_loops`), and checkpoint splits. Weapons are usually disabled.
- **Suitable Maps:** Any map containing checkpoint gates (e.g. `grandprix.xp`, custom race tracks).
- **How to Start:** Enable the race/timing engine flag:
  ```bash
  ./xpilot-tng-server -map grandprix.xp -timing true
  ```
- **End of a race:** after the first player finished, the race goes on for `raceFinishTime` seconds (default 60) so the others can finish too; it ends earlier when nobody races any more. `raceFinishTime 0` keeps the old rules (the race ends with the first finish, with limited lives when at most one player is still racing).
- **Results and records:** when a race ends, TNG clients show a popup with your total time and best lap, the standings of the race and two top ten lists: whole races and single laps. The lists hold human players only, the best time of each nick, and are kept by the server per course (map and checkpoints), number of laps and game speed in `$XDG_DATA_HOME/xpilot-tng/racetimes/` (default `~/.local/share/xpilot-tng/racetimes/`), one text file per combination. Esc, Enter, Space or a click closes the popup; it closes by itself after 30 seconds. The next race starts when every player with a TNG client has closed it (at most 35 seconds); until then all ships wait at their bases.

---

## 6. Ballrace
*Precision obstacle race towing an elastic physics ball.*

- **Objective:** Same as regular race mode, but you must tow a heavy physics ball on an elastic tether through every checkpoint.
- **Rules:** High inertia and tether elasticity (`ballConnectorSpringConstant`, `ballConnectorDamping`) make turns and wall avoidance challenging.
- **Suitable Maps:** Wide checkpoint tracks designed for ball maneuvering.
- **How to Start:** Enable timing along with ballrace options:
  ```bash
  ./xpilot-tng-server -map <race-map> -timing true -ballrace true -ballraceConnected true
  ```

---

## 7. Spaceball
*Zero-gravity space soccer / hockey.*

- **Objective:** Maneuver a special neutral ball into the opponent's goal/base using towing cables, tractor beams, or weapon blasts.
- **Rules:** Fast ships, modified physics, bouncing off walls, and designated scoring areas.
- **Suitable Maps:** `spaceball.xp2`
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map spaceball.xp2
  ```

---

## 8. Tag / "It" Mode
*One hunted player against the rest of the server.*

- **Objective:** One player is designated as "It". Hunting and killing "It" earns large point bonuses and transfers "It" status to the killer.
- **Rules:** Killing "It" applies `tagKillItScoreMult` (default 10x). If "It" kills a pursuer, they get `tagItKillScoreMult` (default 2x).
- **Suitable Maps:** Any open or maze arena, e.g. `blood-music.xp2`, `polybloods.xp2`.
- **How to Start:** Enable `tagGame` on any map:
  ```bash
  ./xpilot-tng-server -map blood-music.xp2 -tagGame true
  ```

---

## 9. Survival / Limited Lives (Last Man Standing)
*Elimination tournament where every life counts.*

- **Objective:** Be the last surviving pilot or team remaining in the arena.
- **Rules:** Each player begins with a fixed pool of lives. When lives reach 0, the player is permanently eliminated until the match resets.
- **Suitable Maps:** `blood-music.xp2`, `circle2.xp2`, `tourmination.xp2`.
- **How to Start:** Set life limits on launch:
  ```bash
  ./xpilot-tng-server -map blood-music.xp2 -limitedLives true -worldLives 3
  ```

---

## 10. Dodgers / PvE Survival
*Evasion challenges against automated turrets and hostile AI.*

- **Objective:** Survive as long as possible while dodging flak cannons, moving hazards, and aggressive smartbots without getting hit.
- **Rules:** Tests pure piloting reflexes, throttle control, and fuel conservation; scoring is tied to survival time (`survivalScore: 1`).
- **Suitable Maps:** `circle2.xp2` (*Circle Cannon Dodgers*), `dodgers-legacy.xp2`, `dodgers-robots.xp2`.
- **How to Start:**
  ```bash
  ./xpilot-tng-server -map circle2.xp2
  # Or dodgers vs bots:
  ./xpilot-tng-server -map dodgers-legacy.xp2
  ```

---

## 11. Timed Match ("Pizza Mode")
*Match with an automated hard time limit.*

- **Objective:** Play any game mode (FFA, Team, CTF) with a countdown timer (e.g. 15 minutes until the pizza arrives).
- **Rules:** When `gameDuration` runs out, the server stops the match and crowns the player/team with the highest score.
- **Suitable Maps:** Applicable to any map and mode.
- **How to Start:** Set the duration in minutes via `-gameDuration` (or `-time`):
  ```bash
  ./xpilot-tng-server -map blood-music.xp2 -gameDuration 15
  ```
