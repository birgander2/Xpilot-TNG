# XPilot TNG – Teamplay

How teams work, how players get into a team, and which options control it.

## 1. How teams are defined

Teams come from the **bases** of a map. Every base belongs to one team (`0`–`9`, `MAX_TEAMS` = 10). A team exists if at least one base carries its number, and the number of bases is the team size. There is no separate "number of teams" setting.

- **XP2 maps:** each base needs a `team` attribute, e.g. `<Base team="4" x="…" y="…" dir="32"/>`. A base without a valid team aborts map loading.
- **Classic `.xp` maps:** the team is part of the base character in the map data. With teamplay on, a base without a team ends up in team 0.
- Team numbers do not have to be consecutive (`blood-music.xp2` uses team 4, for example).

Teams only matter when **`teamPlay`** is on. Without it, team attributes are ignored and everyone shares any free base.

### Maps without teams (automatic split)

If `teamPlay` is on but the map defines at most one team (e.g. `killing.xp2`, where all bases are team 0), the server **randomly splits the bases into two teams**, 0 and 1, of equal size (team 0 gets the extra base for an odd count). The split is redone at every server start. Treasures, targets and fuel stations keep their original team.

Maps that already define two or more teams are never touched.

## 2. Joining a team

1. **Automatic:** without a team request, the server picks one (`Pick_team`). It prefers teams that still have free bases, fills teams that already have active players first, and among equals picks the team with the lowest score.
2. **Request at connect:** `-team <n>` (client option `team`) asks for a specific team. The server falls back to automatic assignment if the team has no bases, is full, is the reserved robot team, or `game_lock` is on.
3. **In game:** `/team <n>` swaps to team *n*. If the team is full, you are queued as a swapper and move once someone swaps back. Operators can move others with `/team <n> <player>`.

### Team selection dialog (SDL3 client)

On a teamplay server with at least two teams, a dialog opens once after joining (unless `-team` was given). It lists each team with `players/bases`, and marks your own team.

- Click a row or press the team's digit key to swap. This sends `/team <n>` for you.
- `Esc` or a click outside closes it.
- **`/team` without a number** in the talk window opens the dialog again.
- The mouse cursor is shown while it is open; mouse steering is restored when it closes.

A reserved robot team (`reserveRobotTeam` with bots enabled) is shown as "(robot team, locked)" and cannot be selected. The dialog does not know about other server-side restrictions: picking a full team or any team under `game_lock` gives the server's usual message.

## 3. Server options

Set on the command line (`-option value`), in `lib/defaults.txt`, or in the map file. The launcher shows most of them (see below).

| Option | Default | Effect |
|---|---|---|
| `teamPlay` | `false` | Enables teams. |
| `teamImmunity` | `true` | Team members are immune to each other's shots, thrust, etc. (also for alliances). |
| `teamFuel` | `false` | Fuel stations belong to teams. |
| `teamCannons` | `false` | Cannons belong to teams. |
| `lockOtherTeam` | `true` | Whether you may watch opposing players while the rest of your team is still alive. |
| `robotTeam` | `0` | Team used by robots (0–9). |
| `restrictRobots` | `true` | Robots stay in `robotTeam`. |
| `reserveRobotTeam` | `true` | Humans cannot join `robotTeam`. |
| `botsMatchHumans` | `false` | Keep the number of bots equal to the number of active humans (see below). |
| `botsPerHuman` | `1.0` | Bots per human when `botsMatchHumans` is on. |
| `robotsFillTo` | `false` | Humans replace bots: the game is filled up to `maxRobots + maxSmartRobots` players in all. With 4 bots wanted and two humans there are two bots, each type keeping its share. Excludes `botsMatchHumans`, which takes precedence. |

Game modes built on teams (`captureTheFlag`, `treasureKillTeam`, `targetKillTeam`, team cup) are described in [GAMEMODES.md](GAMEMODES.md).

### Bots match humans

With `botsMatchHumans` on, the server keeps `humans × botsPerHuman` bots (rounded). A joining human adds a bot, a leaving human removes one. Paused players and spectators do not count. `maxRobots` and `maxSmartRobots` no longer limit the count; they only give the ratio of normal to smart bots (`maxSmartRobots / (maxRobots + maxSmartRobots)` is the chance that a new bot is a smart one). If both are 0, only normal bots are created. New bots are still limited by free bases (and by the bot team's bases with "All bots in one team").

### Robot team pitfalls

If teamplay is on, bots are enabled and `robotTeam` has no bases on the map, the server logs a warning and uses the lowest team number that does have bases (e.g. team 2 on `blood-music.xp2`).

With the defaults (`robotTeam 0`, `restrictRobots`, `reserveRobotTeam`) all bots play in team 0 and humans can only join the other teams. That is the intended "humans vs. bots" setup, but it has consequences:

- On a map split automatically (section 1) humans end up alone in team 1 against the bots in team 0.
- If you set `robotTeam` to a team you want to play in, you get "You cannot join the robot team" until you turn `reserveRobotTeam` off.
- For mixed teams of humans and bots, turn **`reserveRobotTeam` and `restrictRobots` off**.

## 4. Launcher

| Tab | Options |
|---|---|
| Gameplay | Team play, Team immunity |
| Bots | Bot team (`robotTeam`), All bots in one team (`restrictRobots`), No humans in bot team (`reserveRobotTeam`), Bots match humans (`botsMatchHumans`), Bots per human (`botsPerHuman`) |

Changed values are marked with `*`; right-click an option to reset it to the map default. The launcher's server list shows the number of team bases per server in the *Teams* column. This counts bases, not teams.

## 5. Quick recipes

```sh
# Team deathmatch on a team map
./xpilot-tng-server -map blood-music.xp2 -teamPlay true

# Two random teams on a map without teams, humans and bots mixed
./xpilot-tng-server -map killing.xp2 -teamPlay true \
    -reserveRobotTeam false -restrictRobots false

# Join and ask for team 2 directly
./xpilot-tng-client -team 2 <server>
```
