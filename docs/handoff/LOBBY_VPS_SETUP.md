# The lobby server on the Hetzner VPS (one page for the user)

The lobby service only helps the PCs **find and reach** each other (Quick Match, private codes, the hole punch, the relay fallback,
the voice relay). The match itself still runs **directly between the players' PCs** (lockstep peer-to-peer).

## 1. Create the server (Hetzner Cloud console)

- **Image:** Ubuntu 24.04. **Type:** the smallest shared x86 plan. **Location:** Ashburn (us-east).
- **SSH key:** add your own public key (the one on this PC, e.g. `C:\Users\<you>\.ssh\id_ed25519.pub`); no password login.
- **Networking:** a public IPv4 is needed (the game codes are IPv4); IPv6 optional.
- **Firewall** (Hetzner "Firewalls", attach it to the server), inbound rules only:

| protocol | port | from | why |
|---|---|---|---|
| **UDP** | **14010** | any | the lobby service: matchmaking, private codes, the hole punch |
| **UDP** | **14011** | any | the relay: players whose routers cannot connect directly (their match and voice go through it) |
| TCP | 22 | your home IP (or any) | SSH, for deploying / updating |

  Nothing else inbound. Outbound: allow all (default). The service uses no TCP port and no web page.

## 2. Tell us

- The server's **public IPv4** (e.g. `5.161.x.y`).
- That your SSH key works: `ssh root@<ip>` from this PC answers without a password.

## 3. Deploy (we do this, from this PC, with your key)

`bash tools/net/deploy_lobby.sh root@<ip>` copies `lobby_service.py` and a systemd unit, creates a system user `wfclobby` (no login,
no shell), starts the service on boot, logs to `/var/log/wfc-lobby/lobby.log` (rotated daily, 7 days kept). Updates are the same
command. Nothing else is installed (Ubuntu's own Python 3, standard library only). To stop it: `ssh root@<ip> systemctl disable --now wfc-lobby`.

At the end the script runs the health check from this PC, `tools/net/lobby_check.py <ip>:14010`: the lobby answers, a private test
lobby registers and is found by its code, the relay forwards a datagram, the test lobby is removed (nothing left behind). Six lines
ending `LOBBYCHECK SUMMARY: PASS`. A FAIL on the first line or on the relay line almost always means the firewall (section 1).

## 4. In the game

The address ships in the game data: `Rebuild/data/frontend/online_service.json`, `"lobby_service": "<ip>:14010"` (we set it once you send
the IP; the next package carries it). A profile's own `LobbyService=` line in `wfc_profile.ini` overrides it (testing another server).
Empty = no service: LAN and direct-address joins only. There is no in-game entry for it (none is needed).

## 5. Ready checklist (what is done, what waits)

| item | state |
|---|---|
| the service (lobbies, Quick Match, codes, hole punch, the binary relay) | done; 18 + limit tests pass on loopback |
| public-internet limits (per-IP rate, lobbies per IP, total lobbies, list size, relay room / packet caps) | done |
| deploy script (user, unit, hardening, log rotation, health check) | done |
| the game's default address (online_service.json) | done, empty until the IP |
| the server itself, the firewall, your SSH key | **waits for you** (sections 1 and 2) |
| deploy + health check + one internet test match | **we do it**, on your go |

## Limits (abuse protection)

Each sender IP may send 50 requests a second (burst 100), host at most 16 lobbies (generous: many players can share one IP behind a
mobile / carrier NAT), and the server holds at most 4096 lobbies; a public-list answer carries at most 32 games. The relay takes at most
32 players per lobby, 1400-byte packets and 1500 packets a second per IP. Over a limit the request is dropped silently (no bigger answer
bounces back at a forged address), so the server cannot be used to flood someone.

## Cost and load

The smallest plan (~$4-6 / month) carries far more than we need: matchmaking messages are tiny; only players whose routers block
direct connections use the relay (~5-15 KB/s each in a match). Traffic allowance: 20 TB / month.

## Privacy

No accounts, no passwords, nothing stored on disk except the log (time, the lobby codes, the addresses that registered / looked
up a lobby). The log can be turned off by removing `--log ...` from the unit.
