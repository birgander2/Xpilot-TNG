# HOWTO: Hosting an Internet Server Behind a Router (DSL / NAT)

This guide explains how to run an XPilot TNG server on a machine in your home network
(e.g. behind a DSL router) so that other players can find it in the public server list
and join it from the Internet — and *why* each step is necessary.

---

## Table of Contents

1. [How joining a server works](#1-how-joining-a-server-works)
2. [What you need](#2-what-you-need)
3. [Step 1 – A public host name](#3-step-1--a-public-host-name)
4. [Step 2 – Server options](#4-step-2--server-options)
5. [Step 3 – Port forwarding on the router](#5-step-3--port-forwarding-on-the-router)
6. [Step 4 – Firewall on the server machine](#6-step-4--firewall-on-the-server-machine)
7. [Step 5 – Start and verify](#7-step-5--start-and-verify)
8. [Command line / defaults.txt equivalent](#8-command-line--defaultstxt-equivalent)
9. [Troubleshooting](#9-troubleshooting)
10. [Security notes](#10-security-notes)

---

## 1. How joining a server works

Three parties are involved, and each one needs a different address:

```
   Meta server                         Player (client)
 meta.xpilot.org                     (somewhere on the Internet)
        ^   |                                  |
  (1) report |  (2) server list                |
  UDP :5500  v                                 |
        +--------------------+   (3) CONTACT   |
        |  your server       | <---------------+   UDP  contactPort (15345)
        |  (behind router)   |
        |                    | <-------------->    UDP  one port per player
        +--------------------+   (4) game data
```

1. **Reporting.** At least every 3 minutes, and immediately when players join or leave, the
   server sends a short status message (map, players, port, *host name*) to the meta servers `meta.xpilot.org` / `meta2.xpilot.org` (UDP port 5500).
   This is an *outgoing* connection, so it works through any router without configuration.
   The message contains the name given by the option **`serverHost`**.
2. **Listing.** Clients download the server list from the meta server and display it.
   They connect to the **host name and port** found in that list.
3. **Contact.** The client sends a UDP packet to the server's **contact port**
   (`contactPort`, default 15345) to ask for a slot.
4. **Game data.** The server answers by telling the client a *new* UDP port. The server opens
   **one separate UDP socket per player** (see `Create_client_socket` in
   `src/server/netserver.c`). All game traffic runs over that socket.

### Why this breaks behind a router

* **Wrong name (step 1).** Without `serverHost` the server reports the host name of the
  machine it runs on (e.g. `mypc` or `mypc.fritz.box`). Nobody on the Internet can resolve that,
  so players see your server in the list but cannot connect. You must tell the server its
  *public* name.
* **Closed ports (steps 3 and 4).** A router drops unsolicited incoming packets. Packets to the
  contact port arrive only if you **forward** that port to your machine.
* **Random player ports (step 4).** By default the per-player port is chosen randomly by the
  operating system. A port forwarding rule cannot cover a port that is not known in advance, so
  the client would reach the contact port but then get stuck at "connecting". The options
  `clientPortStart` / `clientPortEnd` restrict the server to a **fixed range** that you can
  forward.

---

## 2. What you need

* A router that supports **port forwarding** (called "Port Sharing", "Freigabe" or
  "Virtual Server" depending on the model).
* A **public IPv4 address** for the connection. Check this first, see
  [Troubleshooting](#9-troubleshooting) (many DSL providers use DS-Lite / CGNAT, which makes
  hosting impossible without a VPN or tunnel).
* A **host name** that points to that address (see step 1).
* The server machine should have a **fixed LAN address** (static IP or a DHCP reservation in the
  router), otherwise the forwarding rule points to the wrong machine after a reboot.

---

## 3. Step 1 – A public host name

`serverHost` is documented as the server's *fully qualified domain name*. Use a name, not
a bare IP address:

* If your provider gives you a **fixed IP**, create an `A` record with your domain registrar.
* With a **changing IP** (the usual DSL case) use a dynamic DNS service (DynDNS, No-IP,
  DuckDNS, or the one built into your router, e.g. MyFRITZ). The router updates the name
  whenever the IP changes.

Test it from outside your network (e.g. mobile data) or at least check that it resolves to your
current public address:

```sh
getent hosts play.example.org      # should print your public IP
```

At startup the server tries to resolve the name and logs `Failed name lookup on: ...` if it
cannot. Watch for that line.

---

## 4. Step 2 – Server options

| Option            | Meaning                                                        | Recommended value        |
|-------------------|----------------------------------------------------------------|--------------------------|
| `serverHost`      | Public name reported to the meta server                        | `play.example.org`       |
| `contactPort`     | UDP port where players first contact the server                | `15345` (default)        |
| `clientPortStart` | First UDP port for per-player sockets                          | `15400`                  |
| `clientPortEnd`   | Last UDP port for per-player sockets                           | `15420`                  |
| `reportToMetaServer` | Publish the server in the public list                       | `true` (default)         |

**Size of the port range:** each connected player (including spectators, but not bots) uses
one port from the range. Pick at least as many ports as the maximum number of human players you
expect, plus a few spare. 20 ports are plenty for a private server. A port stays reserved while a
player is connected; if the range is exhausted, new players cannot join and the server logs
`Could not find a usable port in given port range`.

**In the Launcher** (recommended): *Launch Server* → *Advanced settings* → tab **Network**:

* *Public host name* → `serverHost`
* *Client ports from … to …* → `clientPortStart` / `clientPortEnd`

The port of the server (`contactPort`) and *Register at meta server* are in *Quick settings*.
The tab prints the exact ports you have to forward, and the *Command* line at the bottom shows
the resulting server command.

Both ends of the range must be set (start ≤ end, maximum 65535) or both must be 0. The
launcher refuses to start otherwise.

Values already defined in `lib/defaults.txt` are used as the initial values of these fields.

---

## 5. Step 3 – Port forwarding on the router

Create forwarding rules in the router's web interface to the **LAN address of the server
machine**:

| Protocol | External port(s) | Internal port(s) | Purpose                     |
|----------|------------------|------------------|-----------------------------|
| UDP      | 15345            | 15345            | contact port                |
| UDP      | 15400 – 15420    | 15400 – 15420    | per-player game connections |

Rules of thumb:

* The protocol is **UDP** — not TCP. (TCP is only used by *clients* to download the server list.)
* Keep external and internal ports identical. The server announces its own port numbers to the
  client; a rule that translates them (e.g. external 20000 → internal 15345) breaks the connection.
* If you changed `contactPort` (e.g. to run two servers), forward *that* port instead, and use
  a separate port range per server.

---

## 6. Step 4 – Firewall on the server machine

The router forwards the packets, but the firewall of the machine itself must accept them.

**ufw (Ubuntu/Debian):**

```sh
sudo ufw allow 15345/udp
sudo ufw allow 15400:15420/udp
```

**firewalld (Fedora/RHEL/openSUSE):**

```sh
sudo firewall-cmd --permanent --add-port=15345/udp
sudo firewall-cmd --permanent --add-port=15400-15420/udp
sudo firewall-cmd --reload
```

**nftables / iptables:** accept `udp dport 15345` and `udp dport 15400-15420`.

---

## 7. Step 5 – Start and verify

1. Start the server (Launcher: *Launch Server*, or the command from
   [section 8](#8-command-line--defaultstxt-equivalent)).
2. In the log look for:
   * no `Failed name lookup on: ...` line,
   * `Locating Internet Meta server... found 1... found 2`,
   * `Server runs at ... frames per second`.
3. Wait up to 3 minutes (the server reports at least that often) and check from a
   **different network** (mobile hotspot, friend's computer): Launcher → *Join Network Game*
   → *Internet* list. Your server should appear with a ping value. A server without ping
   (`-`) is listed but did not answer the contact packet — i.e. the forwarding or firewall
   is wrong.
4. Join it. If the client reaches "connecting" but never enters the game, the contact port is
   open but the player port range is not (step 3/4 for `15400-15420`).

You can test the contact port without the game from another machine:

```sh
nc -u -z -v play.example.org 15345
```

(UDP probes are only a hint — a "success" does not prove the game answers, but it exposes
obvious firewall problems.)

> **Testing from inside your own LAN:** Many routers do not support "NAT loopback" (reaching
> your own public address from inside). The server then looks offline from your own machine
> even though it works for everybody else. Test from outside, or join the server in your LAN
> using its LAN address (Launcher → *Join Network Game* → *Custom address*).

---

## 8. Command line / defaults.txt equivalent

Without the Launcher:

```sh
./xpilot-tng-server -map ndh.xp2 \
    -serverHost play.example.org \
    -contactPort 15345 \
    -clientPortStart 15400 -clientPortEnd 15420 \
    -noQuit
```

(`-noQuit` keeps the server running when the last player leaves, which is what you want for a
public server.)

To make the settings permanent put them into `lib/defaults.txt`:

```
serverHost:      play.example.org
clientPortStart: 15400
clientPortEnd:   15420
```

Command line options override `defaults.txt`; the Launcher passes only the values you changed
in the GUI.

---

## 9. Troubleshooting

**The server does not show up in the list at all**
* `reportToMetaServer` is off (Launcher: *Register at meta server*).
* The server cannot send UDP to port 5500 (rare: restrictive outgoing firewall).
* The log shows `Locating Internet Meta server... 1 not found`: DNS problem on the server machine.

**It is listed, but with the wrong name / players cannot connect**
* `serverHost` is missing or does not point to your *current* public IP.

**Listed, but ping shows `-`**
* The contact port is not reachable from outside: check router rule (UDP!), firewall, and that
  the rule targets the right LAN address.

**Joining starts, then hangs on "connecting"**
* The per-player port range is not forwarded, or `clientPortStart/End` is not set
  (random ports cannot be forwarded).

**It worked yesterday, not today**
* Dynamic DNS did not update after the IP change, or the LAN address of the server machine changed
  (use a DHCP reservation).

**Forwarding rules have no effect at all (CGNAT / DS-Lite)**
* Compare the WAN address shown in the router with what a "what is my IP" website reports.
  If they differ, or the router's WAN address is in `100.64.0.0/10` or `10.x`/`192.168.x`,
  your provider shares one public IPv4 address between many customers (CGNAT) or provides
  only IPv6 for incoming connections (DS-Lite). Incoming IPv4 connections are then impossible.
  Options: ask the provider for a public IPv4 address (often available for a small fee),
  or host the server on a cheap VPS/VPN with a public address.

---

## 10. Security notes

* Only forward the ports listed above. Do not expose other services of the server machine.
* The server speaks a custom UDP protocol; keep the software up to date.
* Do not run the server as `root`.
* To keep specific addresses out, use the server option `denyHosts` (list of addresses,
  optionally with a network mask).

### Built-in protections

| Option | Default | Effect |
| --- | --- | --- |
| `contactRateBurst` / `contactRatePerSec` | 10 / 2 | Per-IP limit for status and option-list requests on the contact port. Their replies are much larger than the request, so without a limit the server could be abused to flood third parties with forged source addresses. Joining the game is never limited. `contactRateBurst 0` disables the limit. |
| `contactRateLocalExempt` | on | Loopback and private addresses (10.x, 172.16-31.x, 192.168.x) are not limited. Turn it off with `+contactRateLocalExempt` if all players arrive through a proxy or NAT with a private address. |
| `maxClientsPerIP` | 4 | Maximum simultaneous clients (and players joining at the same moment) from one address. Players behind the same router share one address, so raise it (or set 0 for no limit) if groups play together from one network. |
| `contactAdmin` | off | Privileged contact-port requests (lock, shutdown, kick, set option) are refused. They were only protected by a user name check from `127.0.0.1`, which any local user can fake. Use `/password` and `/set` in game instead. |
| `denyHosts` | empty | Addresses (optionally `addr/mask`) that get no service at all. |

Further behavior worth knowing:

* Three wrong `/password` attempts from one address kick the player and lock that address
  out of `/password` for five minutes, also across reconnects. Chat commands count towards
  the flood protection.
* A client must contact the login port from the same address it queued with.
* Choose a strong `password`, preferably in the password file (`passwordFileName`) instead of
  on the command line, where other users of the machine can see it.

### Running as an unprivileged, sandboxed service (systemd)

```ini
# /etc/systemd/system/xpilot-tng-server.service
[Unit]
Description=XPilot TNG server
After=network-online.target

[Service]
ExecStart=/usr/local/bin/xpilot-tng-server -map /usr/local/share/xpilot-tng/maps/mymap.xp2 -noQuit
DynamicUser=yes
StateDirectory=xpilot-tng
WorkingDirectory=/var/lib/xpilot-tng
Restart=on-failure
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
RestrictNamespaces=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
SystemCallFilter=@system-service
CapabilityBoundingSet=

[Install]
WantedBy=multi-user.target
```

The server only needs to write its state (rank file, smartbot brain) into `StateDirectory`.
Adjust the paths to your installation and check `journalctl -u xpilot-tng-server` after the
first start.

### Testing your build

Security regression tests and a parser fuzzer live in `tests/`. They need a sanitizer build:

```sh
cmake -S . -B build-asan -DSANITIZE=ON -DHARDENING=OFF -DBUILD_TESTS=ON \
      -DBUILD_CLIENT_SDL3=OFF -DBUILD_LAUNCHER=OFF
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```
