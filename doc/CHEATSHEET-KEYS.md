# XPilot TNG – Default Keyboard Cheat Sheet

Defaults come from `key_options[]` in `src/client/event.c`. Every key is rebindable via `keyXxx: <keysym list>` in `~/.xpilotrc` (X11 keysym names, space-separated for multiple keys).
Pointer (mouse) defaults: **Button 1 = Fire shot**, **Button 2 / 3 = Thrust**, Buttons 4/5 unbound.

## Flight

| Key | Option | Action |
|---|---|---|
| `A` | keyTurnLeft | Turn left (anti-clockwise) |
| `S` | keyTurnRight | Turn right (clockwise) |
| `Shift` (L/R) | keyThrust | Thrust |
| `Space` | keyShield | Raise/toggle shield (must be **down** to shoot) |
| `Q` | keyHyperJump | Teleport (hyperjump item) |
| `P` | keyPhasing | Phasing device |
| `Delete` / `Backspace` | keyCloak | Toggle cloak |
| `O` | keyDeflector | Toggle deflector |
| `J` | keyEmergencyThrust | Emergency thrust |
| `Caps Lock` | keyEmergencyShield | Emergency shield |
| `9` | keyToggleAutoPilot | Toggle autopilot |
| `End` | keySelfDestruct | Toggle self-destruct |
| `Pause` | keyPause | Pause (only stationary on home base) |
| `Home` | keyChangeHome | Change home base (when near a suitable base) |

## Weapons

| Key | Option | Action |
|---|---|---|
| `Enter` / `Linefeed` | keyFireShot | Fire shot |
| `\` | keyFireMissile | Smart missile |
| `'` | keyFireTorpedo | Unguided torpedo |
| `;` | keyFireHeat | Heat-seeking missile |
| `/` | keyFireLaser | Laser |
| `Tab` | keyDropMine | Drop stationary mine |
| `]` | keyDetachMine | Detach moving mine |
| `=` | keyDetonateMines | Detonate nearest own mine |
| `[` | keyEcm | ECM |
| `T` | keyTransporter | Transporter (steal item) |
| `,` | keyTractorBeam | Tractor beam (attract) |
| `.` | keyPressorBeam | Tractor beam (repulse / pressor) |

## Weapon modifiers

| Key | Option | Modifier |
|---|---|---|
| `N` | keyToggleNuclear | Nuclear |
| `C` | keyToggleCluster | Cluster |
| `I` | keyToggleImplosion | Implosion |
| `V` | keyToggleVelocity | Explosion velocity |
| `X` | keyToggleMini | Mini |
| `Z` | keyToggleSpread | Spread |
| `B` | keyTogglePower | Power |
| `L` | keyToggleLaser | Laser modifier (stun / blind) |
| `K` | keyClearModifiers | Clear all modifiers |
| `1` `2` `3` `4` | keyLoadModifiers1–4 | Load modifier bank 1–4 |
| `` ` `` | keyReprogram | Hold + bank key to reprogram a modifier/lock bank |

## Targeting / locks

| Key | Option | Action |
|---|---|---|
| `↑` | keyLockClose | Lock closest player |
| `↓` | keyLockNextClose | Lock next closest |
| `PgDn` / `→` | keyLockNext | Lock next player |
| `PgUp` / `←` | keyLockPrev | Lock previous player |
| `5` `6` `7` `8` | keyLoadLock1–4 | Load player lock from bank 1–4 |

## Fuel, tanks, balls

| Key | Option | Action |
|---|---|---|
| `F` / `Ctrl` | keyRefuel | Refuel |
| `F` | keyRepair | Repair target |
| `F` / `Ctrl` | keyConnector | Connect to ball |
| `D` | keyDropBall | Drop ball |
| `E` | keyTankNext | Next tank |
| `W` | keyTankPrev | Previous tank |
| `R` | keyTankDetach | Detach current tank |

## Tuning (keypad)

| Key | Option | Action |
|---|---|---|
| `KP *` / `KP /` | keyIncreasePower / keyDecreasePower | Engine power ± |
| `KP +` / `KP -` | keyIncreaseTurnspeed / keyDecreaseTurnspeed | Turn speed ± |
| – (unbound) | keySwapSettings | Swap to alternate power/turn settings |
| – (unbound) | keySwapScaleFactor | Swap scale factor |

## Items & HUD

| Key | Option | Action |
|---|---|---|
| `KP 0` / `KP Ins` | keySelectItem | Select item to lose |
| `KP Del` / `KP .` | keyLoseItem | Lose selected item |
| `KP 7` | keyToggleCompass | HUD/radar compass lock |
| `KP 8` | keyToggleOwnedItems | Owned-items list on HUD |
| `KP 9` | keyToggleMessages | Show/hide messages |
| `U` | keyIdMode | Show real names (user mode) |
| `Print` | keyPrintMessagesStdout | Dump messages to stdout |
| `KP 5` | keyToggleRecord | Toggle session recording |
| `KP Enter` | keyPointerControl | Toggle pointer (mouse) control |
| – (unbound) | keyToggleSound | Toggle sound |

## Display & View

| Key | Option | Action |
|---|---|---|
| `F11` | keyToggleFullScreen | Toggle between windowed mode and fullscreen |
| `F12` | keyToggleRadarScore | Toggle sidebar (radar & scoreboard) on/off to expand playfield |

> **Display Tip:** Combining `F11` (Fullscreen) and `F12` (Hide Sidebar) provides a completely borderless, edge-to-edge playfield across your entire monitor.
> These can also be passed as client startup flags (`-fullScreen`, `-showSidebar false`) or configured in `~/.xpilotrc` (`fullScreen: true`, `showSidebar: false`).

## Chat & dialogs

| Key | Option | Action |
|---|---|---|
| `M` | keyTalk | Open/close talk window |
| `← → ↑ ↓` | keyTalkCursor* | In talk window: move cursor / browse history |
| `F1`–`F10` | keySendMsg1–10 | Send stored talk macro `msg1`–`msg10` |
| – (unbound) | keySendMsg11–20 | Talk macros 11–20 |
| `Esc` | keyExit | Exit mouse mode / quit client |
| `Y` / `N` | keyYes / keyNo | Answer client questions |

## Voice chat

| Key | Option | Action |
|---|---|---|
| `G` (hold) | keyVoiceTeam | Push-to-talk on the team channel (all channel on servers without teams) |
| `H` (hold) | keyVoiceAll | Push-to-talk on the channel everybody nearby hears |

> Voice activation is on by default (`voiceActivation: false` = push-to-talk only). Mute a player with `\ignore <name>`. See [VOICECHAT.md](VOICECHAT.md).

> **Note:** several defaults overlap (`F` for refuel/repair/connect, arrows for lock vs. talk cursor); the context decides which fires.
