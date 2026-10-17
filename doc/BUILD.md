# Building XPilot TNG

## Requirements

- C17/C23 compiler (GCC 15 ready), C++20 compiler (launcher)
- CMake ≥ 3.25 (Ninja optional)
- zlib, expat
- SDL3 and SDL3_ttf (client, launcher). If they are not installed, let CMake
  download and build them: `-DFETCH_SDL3=ON`
- The launcher fetches Dear ImGui via CMake `FetchContent` (needs network on
  first configure)

## Quick start

```sh
make -j$(nproc)          # configure + build into build/
```

or directly with CMake:

```sh
cmake -B build
cmake --build build -j$(nproc)
```

The binaries `xpilot-tng-server`, `xpilot-tng-client` and `xpilot-tng-launcher`
end up directly in the build directory. `make` additionally creates symlinks
for server and client in the project root, so you can run them from there:

```sh
./xpilot-tng-server -map lib/maps/ndh.xp2 -port 15345
./xpilot-tng-client localhost -port 15345 -name Maverick -join
```

## CMake options

| Option               | Default | Meaning                                         |
|----------------------|---------|-------------------------------------------------|
| `BUILD_SERVER`       | ON      | Build `xpilot-tng-server`                       |
| `BUILD_CLIENT_SDL3`  | ON      | Build `xpilot-tng-client`                       |
| `BUILD_LAUNCHER`     | ON      | Build `xpilot-tng-launcher` (needs SDL3)        |
| `FETCH_SDL3`         | OFF     | Download and build SDL3 / SDL3_ttf if missing   |
| `DIST_DIR`           | `<build>/dist` | Target of the `dist` bundle (see below)  |

Example: `cmake -B build -DBUILD_LAUNCHER=OFF -DFETCH_SDL3=ON`

## Choosing the output directory

**Build directory** (where the binaries are created, default `build/`):

```sh
make BUILD_DIR=out/release
# or
cmake -B out/release && cmake --build out/release -j$(nproc)
```

**Install** (default prefix `/usr/local`):

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/xpilot
cmake --build build && cmake --install build
# or choose the prefix at install time:
cmake --install build --prefix /opt/xpilot
```

Binaries go to `<prefix>/bin`, game assets (`lib/`) to
`<prefix>/share/xpilot-tng/`.

## Relocatable bundle

The `dist` target creates a self-contained directory with the binaries and
`lib/` side by side. It can be moved, archived or copied to another machine
(same OS/architecture, SDL3 available) without rebuilding.

```sh
cmake --build build --target dist                    # → build/dist/
cmake -B build -DDIST_DIR=$HOME/xpilot-tng           # custom location
cmake --build build --target dist
```

Result:

```
dist/
├── xpilot-tng-server
├── xpilot-tng-client
├── xpilot-tng-launcher
└── lib/            maps, textures, sounds, fonts, defaults.txt, ...
```

Only the binaries that were actually built are copied. Re-run the target
after rebuilding to refresh the bundle.

### How it works

At startup, server, client and launcher check whether `lib/maps` exists next
to the executable. If so, they switch their working directory to the
executable's directory, so all `lib/...` paths resolve – regardless of where
you start the program from:

```sh
cd /somewhere/else && /path/to/dist/xpilot-tng-launcher
```

In a normal development build (`build/` without a `lib/` next to the
binaries) nothing changes. Executable detection uses `/proc/self/exe` and is
currently Linux-only.

## Where are the game data looked up?

Lookup order for the data directory (`lib/`), see
[src/common/config.c](../src/common/config.c):

1. `$XPILOT_DATADIR`, if set and readable
2. `lib/` in the current working directory
3. `../lib/` relative to the working directory
4. The installed path baked in at configure time
   (`<prefix>/share/xpilot-tng/`)

The bundle mechanism above runs before this, by changing the working
directory, so a bundle is found via step 2. If you move an *installed*
(`cmake --install`) tree, set `XPILOT_DATADIR` to its new data directory.

## User files

| File | Location (default) |
|---|---|
| Client config (xpilotrc) | `$XDG_CONFIG_HOME/xpilot-tng/xpilotrc` → `~/.config/xpilot-tng/xpilotrc` |
| Smartbot brain | `$XDG_DATA_HOME/xpilot-tng/smartbot_brain.dat` → `~/.local/share/xpilot-tng/` |

The xpilotrc lookup order is: `$XPILOTRC`, the XDG path above (if it exists),
the legacy `~/.xpilotrc` (if it exists), otherwise the XDG path, which is
created when the client saves its configuration. To migrate:
`mkdir -p ~/.config/xpilot-tng && mv ~/.xpilotrc ~/.config/xpilot-tng/xpilotrc`.

## Cleaning

```sh
make clean               # removes build/ and the root symlinks
```
