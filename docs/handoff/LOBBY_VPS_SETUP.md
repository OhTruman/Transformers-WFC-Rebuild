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
| **UDP** | **14010** | any | the lobby service: matchmaking, the hole punch, the relay and voice relay - all on this one port |
| TCP | 22 | your home IP (or any) | SSH, for deploying / updating |

  Nothing else inbound. Outbound: allow all (default). The service uses no TCP port and no web page.

## 2. Tell us

- The server's **public IPv4** (e.g. `5.161.x.y`).
- That your SSH key works: `ssh root@<ip>` from this PC answers without a password.

## 3. Deploy (we do this, from this PC, with your key)

`bash tools/net/deploy_lobby.sh root@<ip>` copies `lobby_service.py` and a systemd unit, creates a system user `wfclobby` (no login,
no shell), starts the service on boot, logs to `/var/log/wfc-lobby/lobby.log`. Updates are the same command. Nothing else is installed
(Ubuntu's own Python 3, standard library only). To stop it: `ssh root@<ip> systemctl disable --now wfc-lobby`.

## 4. In the game

Settings -> the Lobby Service entry (Browse / Quick Match screen): `<ip>:14010`. Empty = LAN / private codes only.

## Cost and load

The smallest plan (~$4-6 / month) carries far more than we need: matchmaking messages are tiny; only players whose routers block
direct connections use the relay (~5-15 KB/s each in a match). Traffic allowance: 20 TB / month.

## Privacy

No accounts, no passwords, nothing stored on disk except the log (time, the lobby codes, the addresses that registered / looked
up a lobby). The log can be turned off by removing `--log ...` from the unit.
