# XPilot Legacy Documentation (doc/)

This directory contains the historical documentation of **XPilot** and **XPilot NG**, ported from the original plain-text documentation in [`legacy/xpilot-ng-4.7.3/doc`](../legacy/xpilot-ng-4.7.3/doc/) into structured Markdown files (`.md`) inside the [`legacy/`](legacy/) subfolder.

---

## Documentation Index

| Document | Original File | Description |
| :--- | :--- | :--- |
| [**FAQ.md**](legacy/FAQ.md) | `FAQ` | Frequently Asked Questions (28 Q&A) covering gameplay, installation, controls, network protocol, and configuration. |
| [**README.MAPS.md**](legacy/README.MAPS.md) | `README.MAPS` | Specification of map and configuration file formats, precedence hierarchy (CLI vs. map vs. defaults), syntax rules (`\override:`, `\multiline:`), and reference table of all 128 server options. |
| [**README.MAPS2.md**](legacy/README.MAPS2.md) | `README.MAPS2` | Map 2.x tile symbol legend (solid blocks, cannons, team bases, gravity streams, wormholes, and item concentrators). |
| [**README.SHIPS.md**](legacy/README.SHIPS.md) | `README.SHIPS` | Custom ship shapes (`-shipShape`), geometric requirements, and syntax specifications (legacy v3.1.0 and modern v3.2.0+). |
| [**README.passwords.md**](legacy/README.passwords.md) | `README.passwords` | Player password authentication system (`/setpass`, `/auth`) and associated server configuration options. |
| [**README.rank.md**](legacy/README.rank.md) | `README.rank` | Setup guide for configuring an XPilot NG ranking server, including Expat & zlib prerequisites and HTML web export. |
| [**README.sounds.md**](legacy/README.sounds.md) | `README.sounds` | Audio subsystem architecture, supported audio drivers (rplay, NCD, DEC AudioFile, HP), and sound events reference table. |
| [**README.talkmacros.md**](legacy/README.talkmacros.md) | `README.talkmacros` | In-game chat macros ("Talk Patch" manual v1.2), key bindings, and substitution variables (`%h`, `%l`, etc.) in `.xpilotrc`. |
| [**ChangeLog.md**](legacy/ChangeLog.md) | `ChangeLog` | Detailed changelog for XPilot NG versions 4.7.0, 4.5.4X, and 4.3.1X (client, server, GUI, and maps). |
| [**CREDITS.md**](legacy/CREDITS.md) | `CREDITS` | Complete contributors list from versions 1.2 to 4.5 including the `xpilot.org` core team. |
| [**CREDITS-NG.md**](legacy/CREDITS-NG.md) | `CREDITS-NG` | Contributors for the XPilot NG 4.3X release. |
| [**BUGS.md**](legacy/BUGS.md) | `BUGS` | Bug tracker format specification and unresolved legacy bug entries. |
| [**FIXED.md**](legacy/FIXED.md) | `FIXED` | Historical archive of resolved bugs and developer discussions from XPilot 1.x to 3.x. |
| [**TODO.md**](legacy/TODO.md) | `TODO` | Conceptual feature proposals (territory conquest mode, shared team lives, laser fences, and ASCII layout sketch). |
| [**MAN-PAGES.md**](legacy/MAN-PAGES.md) | `man/README` | Instructions for viewing local roff manual pages with `nroff -man` and generation notes. |

---

## Related Directories

- Converted legacy docs: [`doc/legacy/`](legacy/)
- Original plain-text sources: [`legacy/xpilot-ng-4.7.3/doc/`](../legacy/xpilot-ng-4.7.3/doc/)
- Legacy codebase: [`legacy/`](../legacy/)
