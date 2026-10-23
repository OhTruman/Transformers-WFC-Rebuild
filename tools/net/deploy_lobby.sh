#!/usr/bin/env bash
# Deploy / update the WFC lobby service on the VPS (docs/handoff/LOBBY_VPS_SETUP.md). Run from this PC (Git Bash), with the user's
# own SSH key already authorised on the server:   bash tools/net/deploy_lobby.sh root@<server-ip>
# Idempotent: the first run creates the user / folders / service; later runs copy the new lobby_service.py and restart it.
set -euo pipefail
TARGET="${1:?usage: deploy_lobby.sh root@<server-ip>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
scp -q "$HERE/lobby_service.py" "$HERE/wfc-lobby.service" "$TARGET:/tmp/"
ssh "$TARGET" bash -s <<'EOF'
set -euo pipefail
id wfclobby >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin wfclobby
install -d -o root -g root -m 755 /opt/wfc-lobby
install -d -o wfclobby -g wfclobby -m 750 /var/log/wfc-lobby
install -o root -g root -m 644 /tmp/lobby_service.py /opt/wfc-lobby/lobby_service.py
install -o root -g root -m 644 /tmp/wfc-lobby.service /etc/systemd/system/wfc-lobby.service
rm -f /tmp/lobby_service.py /tmp/wfc-lobby.service
systemctl daemon-reload
systemctl enable --now wfc-lobby
systemctl restart wfc-lobby
sleep 1
systemctl --no-pager --lines=5 status wfc-lobby
EOF
echo "deployed to $TARGET: UDP 14010 (lobby) + 14011 (relay) - open both in the VPS firewall"
