# XPilot TNG Launcher

`xpilot-tng-launcher` is a graphical front end (Dear ImGui + SDL3) for starting games without
typing command lines. It can

* start a **local game** (your own server with bots plus a client),
* **host a server** for other players,
* **browse** servers in the local network and on the Internet and join them.

The launcher does not contain any game logic. It starts the normal `xpilot-tng-server` and
`xpilot-tng-client` programs as child processes and shows you exactly which command lines it uses.

---

## Table of Contents

1. [Building and starting](#1-building-and-starting)
2. [Main menu](#2-main-menu)
3. [Start Local Game](#3-start-local-game)
4. [Launch Server](#4-launch-server)
5. [Join Network Game](#5-join-network-game)
6. [How options are applied](#6-how-options-are-applied)
7. [Files and programs the launcher uses](#7-files-and-programs-the-launcher-uses)
8. [Keyboard shortcuts](#8-keyboard-shortcuts)
9. [Troubleshooting](#9-troubleshooting)
10. [Source overview](#10-source-overview)

---

## 1. Building and starting

The launcher is built together with the rest of the project (CMake option
`BUILD_LAUNCHER`, default `ON`):

```sh
cmake -B build
cmake --build build -j$(nproc)
./xpilot-tng-launcher            # symlink created by the build, or ./build/xpilot-tng-launcher
```

Requirements: SDL3, expat, a C++20 compiler. Dear ImGui is downloaded by CMake at configure
time (`FetchContent`, needs network access once). The launcher uses POSIX interfaces
(`fork`, sockets) and runs on Linux and similar systems.

Start it from the project directory or from anywhere else: it looks for the directory that
contains `lib/maps` (current directory, the directory of the launcher binary, and its parent
directories) and changes into it, because the server resolves `lib/` relative to its working
directory.

While a game is running the launcher window is **hidden** and returns when the client exits.

---

## 2. Main menu

| Button                     | Action                                              | Key |
|----------------------------|-----------------------------------------------------|-----|
| Start Local Game           | Play against bots on this machine                   | `L` |
| Join Network Game          | Browse LAN and Internet servers                     | `J` |
| Launch Server              | Host a game for other players                       | `S` |
| Manage Running Server      | Replaces *Launch Server* while a server is running  | `S` |
| Quit                       | Exit (asks first if a server is still running)      | `Q` |

`Esc` goes back one level on every screen.

---

## 3. Start Local Game

The screen has three areas.

**Left – map.** A search field, a preview card (thumbnail, title, author, size, number of bases
and cannons) and the list of all maps in `lib/maps` (`.xp2` vector maps and classic `.xp`
maps). Click a map to select it.

**Right – settings.**

* *Quick settings*
  * *Name* and *Ship shape*: the nickname and ship the client uses. The defaults come from
    `~/.xpilotrc` (and your account name). The ship entry reads *(from .xpilotrc)* if the
    file defines a ship and *(client default)* otherwise — in that case the client uses its
    built-in ship. Choosing a ship from the list passes it to the client explicitly.
  * Bots, Smartbots and Smartbot level, Lives (0 = unlimited), Frames per second, Item
    frequency.
* *Advanced settings* – tabs **Gameplay**, **Physics**, **Weapons & Items**, **Bots**,
  **Map Options** (every option the selected map file defines) and **Network**:
  * *Server port* (0 = pick a free port automatically)
  * *Report to meta server* – publish the local game in the public list (**off** by default)
* *Log* – output of server and client, filterable, with auto-scroll.

**Bottom.** Status lights for server and client and the big **PLAY** button.

What happens on *PLAY*:

1. A server is started on the chosen/free port with the selected map.
2. The launcher waits until the server prints *Server runs at …* (timeout: 15 s).
3. The client is started and connects to `localhost`. The launcher window hides.
4. When the client exits, the launcher returns to the menu. Three seconds later the local
   server is stopped automatically if nobody is left on it. The server is also started with
   `+noQuit` so that it ends by itself when the last player has left.

If a server is already running when you press *PLAY*, a dialog asks whether to
*Join current game*, *Relaunch new game* (stops the old server and disconnects everyone) or
*Cancel*.

---

## 4. Launch Server

The same layout as the local game, but without client and PLAY. Differences:

* *Quick settings* start with the server row: **Port**, **Max players** (0 = number of bases
  + 10), **Register at meta server** (on by default) and a **Greeting** message.
* The **Network** tab contains the settings for hosting behind a router — *Public host name*
  (`serverHost`) and the *Client ports from … to …* range (`clientPortStart` / `clientPortEnd`).
  The tab shows which ports to forward. The complete background is in
  [HOWTO-INTERNET-SERVER.md](HOWTO-INTERNET-SERVER.md).
* The bottom bar shows the complete **Command** that will be run, with a *Copy* button. You can
  use it to start the same server later without the launcher.
* **LAUNCH SERVER** starts the server and opens the *Server* screen.

The **Server** screen shows the map, port, uptime, the list of connected players and bots
(name, type, team, host) and the server log. **STOP SERVER** ends it; *< Menu* leaves the
screen and keeps the server running (come back via *Manage Running Server*).

Quitting the launcher while a server runs asks for confirmation and then stops the server.

---

## 5. Join Network Game

Name and ship shape are set at the top (same as above), then three boxes:

* **Local network** – found by a UDP broadcast on port 15345. Servers on this machine are
  shown as `localhost`.
* **Internet** – the list from the meta servers (`meta.xpilot.org`, `meta2.xpilot.org`),
  followed by a ping measurement. Servers that do not answer the ping show `-`. Rows are sorted
  by ping.
* **Custom address** – enter `host` or `host:port` (default port 15345).

Both lists refresh automatically when the screen opens; use *Refresh* to repeat the search.
Click a row (or use the custom address field) to choose the target, then press **PLAY** or
double-click a row. The *Command* line shows the client command for the current choice, with
a *Copy* button.

Hover over a row for the server version and the map author. Columns: server, map, players /
free bases, number of team bases (*Teams*), status, ping in ms.

---

## 6. How options are applied

The option editor does not start the server with a long list of arguments. It works like this:

```
baseline = compiled defaults (server -dump)  <-  lib/defaults.txt  <-  map file
current  = baseline + what you changed in the GUI
```

* Only options that **differ from the baseline** are passed on the command line. Everything
  you did not touch behaves exactly as with a plain server start (`defaults.txt` and the map
  file stay in charge).
* Command line options have the highest priority in the server, so your edits win.
* An option you changed is marked with `*`. **Right-click** it and choose *Reset to map
  default* to undo.
* Choosing a **different map** discards all edits for the old map. Re-entering a screen with
  the same map keeps them.
* Settings that belong to the *mode* (port, max players, greeting, meta server, host name, port
  range for the server; local port and meta server for the local game) are kept across map
  changes and are applied to the command line when launching.
* If the server defaults cannot be read (`xpilot-tng-server -dump` fails) the launcher says so
  and only the *Map Options* tab remains.

Not saved: the launcher does not write a configuration file. Your choices (name, ship, ports,
…) are reset when you restart it.

---

## 7. Files and programs the launcher uses

| What                        | Where                                                   |
|-----------------------------|---------------------------------------------------------|
| Server / client binaries    | project directory, `build/`, directory of the launcher, its parent, `/usr/local/bin`, `/usr/bin`, finally the `PATH` |
| Maps                        | `lib/maps/*.xp2`, `lib/maps/*.xp`                       |
| Server defaults             | `lib/defaults.txt`                                      |
| Ship shapes                 | `lib/shipshapes.txt` (the names offered in the list)    |
| Nickname and ship           | `~/.xpilotrc` (`xpilot.name`, `xpilot.shipShape`)       |

The launcher captures the output of the programs it starts (stdout and stderr) and shows it in
the log. It also reads two kinds of marker lines from the server output to track players:
`[join] name|host|kind|team|id`, `[leave] name|id` and `Server runs at …` for readiness.

---

## 8. Keyboard shortcuts

| Key        | Where            | Action                                    |
|------------|------------------|-------------------------------------------|
| `L` `J` `S` `Q` | Main menu  | see [section 2](#2-main-menu)             |
| `Esc`      | all screens      | back to the previous level / close dialog |
| `Enter`    | custom address   | join the entered address                  |
| Arrow keys, `Tab`, `Space` | all | keyboard navigation of the widgets     |

Single-letter shortcuts are ignored while you type into a text field or while a dialog is open.

---

## 9. Troubleshooting

**"The local game could not be started" / the server exits right away**
Open the *Log*. The server's own error message is shown there (for example a wrong option or an
unreadable map).

**"Port … is already in use"**
Choose another port (*Advanced settings → Network*) or leave it at 0 for automatic selection.

**"The client could not be started"**
The client binary was not found. Build the project (`cmake --build build`) or run the
launcher from the project directory.

**"Server defaults could not be read"**
`xpilot-tng-server -dump` failed. Check that the server binary exists and runs.

**No servers found on the local network**
Broadcasts are blocked by some firewalls and VPNs. Use *Custom address* with the IP address of
the server.

**My server is not visible on the Internet / nobody can join**
See [HOWTO-INTERNET-SERVER.md](HOWTO-INTERNET-SERVER.md).

---

## 10. Source overview

All code is in `src/launcher/`.

| File                              | Purpose                                                          |
|-----------------------------------|------------------------------------------------------------------|
| `main.cpp`                        | SDL window, fonts, theme, main loop, show/hide while playing     |
| `launcher_app.{h,cpp}`            | Screens, navigation and the start/stop logic                     |
| `options_model.{h,cpp}`           | Baseline + map + edits, building the server command line, ports  |
| `process_manager.{h,cpp}`         | Starting/stopping server and client, output capture, player list |
| `server_browser.{h,cpp}`          | LAN broadcast search, meta server list, ping (background thread) |
| `client_profile.{h,cpp}`          | Nickname and ship shape, client arguments                        |
| `ui_helpers.{h,cpp}`              | Shell quoting, command line display, small ImGui helpers         |
| `map_preview.{h,cpp}`             | Reads `.xp2` / `.xp` maps and renders the 256×256 thumbnails     |
