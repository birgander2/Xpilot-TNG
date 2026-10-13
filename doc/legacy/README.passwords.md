# XPilot Player Passwords (README.passwords)

> [!NOTE]
> Ported from `legacy/xpilot-ng-4.7.3/doc/README.passwords`.
> 
> [← Back to Documentation Index](../OLD_DOCS.md)

## Overview

Player passwords have been supported in XPilot since version **4.3.1X**.

Players can set a password for their current nick or change it using the `/setpass` command. On login, their nick will be changed and they have to authenticate using the `/auth` command.

Commands can be invoked by typing them in the chat message input (default key `m`).
- `/help` — Lists all available commands.
- `/help <command>` — Displays help for a specific command.

## Server Options

| Option Name | Short Name | Default Value | Description |
| :--- | :--- | :--- | :--- |
| `allowPlayerPasswords` | `PlayerPasswords` | `False` | Enables or disables player password authentication. |
| `playerPasswordsFileName` | `playerPasswordsFile` | `<LIBDIR>/player_passwords` | File path where passwords are stored (`LIBDIR` defaults e.g. to `/usr/local/lib/xpilot`). |
| `playerPasswordsFileSizeLimit` | `playerPasswordsLimit` | `1000000` (1 MB) | Maximum size limit of the passwords file. |

## Notes

- The passwords file size can grow slightly beyond the limit (by a small percentage) because the limit check is only enforced when a *new* password is added.
- Existing password changes and logins update entries regardless of whether the file size limit has been reached.
