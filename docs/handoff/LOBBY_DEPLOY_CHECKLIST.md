# Lobby service deploy: the ten-minute checklist (Systems, 2026-10-23)

The one-page background is `LOBBY_VPS_SETUP.md`. This is the exact sequence, every command included, for the day the user says go.

## Dry run (done 2026-10-23, on this PC)

| check | result |
|---|---|
| `deploy_lobby.sh` shell syntax (`bash -n`) | OK |
| `lobby_service.py` / `lobby_check.py` parse as Python 3.8 syntax; standard library only | OK (Ubuntu 24.04 ships Python 3.12; this PC's tool Python is 3.12.7, the same version) |
| the unit's ExecStart (`python3 -I lobby_service.py --bind <ip>:14010 --log <file>`) on loopback, then `lobby_check.py` against it | PASS (ping, test lobby, lookup, relay forward, unregister); the log file is written |
| the service tests (`lobby_service_test.py`, `invite_check.py`) | PASS |
| a real systemd / useradd / logrotate run | **not possible here**: no Linux on this PC (WSL has no distribution; installing one changes the PC, so not done). These steps run first time on the VPS; the script prints `systemctl status` so a failure shows at once |

**SSH key: none on this PC yet.** Git Bash's home here is `C:\SPB_Data` (not the user profile), and `deploy_lobby.sh` runs in Git
Bash, so the key must be made in Git Bash (step 1). Then it lands in `C:\SPB_Data\.ssh`, where `ssh` / `scp` in the script look.

## 1. The SSH key (this PC, Git Bash, once)

```
ssh-keygen -t ed25519 -C "wfc-lobby-deploy"      # Enter for the default path (/c/SPB_Data/.ssh/id_ed25519); a passphrase is optional
cat ~/.ssh/id_ed25519.pub                         # copy this one line for step 2
```

## 2. The server (the user, Hetzner Cloud console)

1. **Security > SSH keys > Add:** paste the line from step 1.
2. **Firewalls > Create:**
   - inbound **UDP 14010** from any (lobby);
   - inbound **UDP 14011** from any (relay);
   - inbound **TCP 22** from your home IP (or any).
   - Nothing else.
3. **Servers > Add:**
   - Ubuntu 24.04, the smallest shared x86 type, location Ashburn (or nearest to the players);
   - IPv4 on;
   - the SSH key from 2.1;
   - the firewall from 2.2.
4. Send us the server's **IPv4**.

## 3. Deploy (us, Git Bash, from the worktree)

```
ssh -o StrictHostKeyChecking=accept-new root@<IP> 'python3 -V && uname -a'   # first contact: accepts the host key, shows Python 3.12
bash tools/net/deploy_lobby.sh root@<IP>                                       # user, folders, unit, logrotate, start, status, health check
```

The script ends with the health check. Expect six `LOBBYCHECK` lines ending `LOBBYCHECK SUMMARY: PASS`.

- **A FAIL on the first line:** UDP 14010 is not open (Hetzner firewall).
- **A FAIL on the relay line:** UDP 14011 is not open.

The check can be re-run any time: `"F:/Transformers Rebuild/AssetTools/bin/py/python.exe" -I tools/net/lobby_check.py <IP>:14010`.

## 4. Point the game at it (us)

1. `data/frontend/online_service.json`: `"lobby_service": "<IP>:14010"` (one line; commit; Integration merges).
2. The next package carries it (build_slim ships data/frontend from the head).

Already-installed packages can be pointed at it without a rebuild: add `LobbyService=<IP>:14010` to `wfc_profile.ini`.

## 5. One internet test (two PCs on different networks, or one PC + a phone hotspot)

1. Host a private game: the lobby shows a 6-letter code.
2. The other PC joins with the code.
3. On the server, watch the joins happen:

   ```
   ssh root@<IP> tail -f /var/log/wfc-lobby/lobby.log
   ```

   It shows `register` / `lookup` lines.

A pair behind strict NATs shows `relay` traffic in the game logs (`session: ... through the relay`).

## Operations

| what | command |
|---|---|
| status | `ssh root@<IP> systemctl status wfc-lobby` |
| log | `ssh root@<IP> tail -n 100 /var/log/wfc-lobby/lobby.log` (rotated daily, 7 kept) |
| update after a service change | `bash tools/net/deploy_lobby.sh root@<IP>` (same command; restarts it) |
| stop / disable | `ssh root@<IP> systemctl disable --now wfc-lobby` |
| remove completely | `ssh root@<IP> 'systemctl disable --now wfc-lobby; rm -rf /opt/wfc-lobby /etc/systemd/system/wfc-lobby.service /etc/logrotate.d/wfc-lobby /var/log/wfc-lobby; userdel wfclobby'` |
| no logging at all | edit `tools/net/wfc-lobby.service` (drop `--log ...`), redeploy |
