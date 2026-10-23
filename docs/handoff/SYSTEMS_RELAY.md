# Relay fallback (Systems, 2026-10-23)

Internet play is peer to peer: the join code carries the host's public and LAN endpoints, UPnP opens the host's port, STUN learns
the public one, and the lobby service's punch notice makes both sides send first (the UDP hole punch). That fails between two
**symmetric NATs** (the router picks a new outside port per destination; some mobile / CGNAT networks). Then the PCs reach each
other through a relay on the VPS. This is the design.

## 1. What exists

- `tools/net/lobby_service.py` has a JSON `relay` op (`{to, data: base64}` -> `{op: relayed, from, data}`) with an allow-list. The
  game never uses it, and base64 JSON per packet (+40 % size, a JSON parse per datagram) is too heavy for the lockstep's ~60
  packets a second per peer plus voice. It stays for compatibility; the game uses the binary relay below.
- The session layer treats peer addresses as opaque `"ip:port"` strings everywhere (JOIN, the PEERS table, the match view).

## 2. The relay

- **Where:** inside `lobby_service.py` (the same process, the same deploy unit and systemd service), on its own UDP port
  (`--relay-port`, default the lobby port + 1 = 14011). One process, so the service's lobbies are the relay's rooms.
- **Rooms and tokens:** `register` returns, besides the code, `room` (a random 64-bit token) and `relay` (the relay's
  `ip:port` as the client should reach it). `lookup` / `quick` candidates return the same `room` + `relay` for that lobby. A
  datagram for an unknown room is dropped: only clients that got the token from the service can use the relay, and a room dies
  with its lobby (15 s after the host's last register).
- **Datagram:** `b"WR" | room u64 | src u32 | dst u32 | payload`. `src` / `dst` are relay ids (random u32 each client picks per
  session; 0 = none). `dst == 0` = register / keepalive (no payload). The relay learns `(room, src) -> the sender's address` from
  every datagram (so NAT rebinding just works) and forwards the whole datagram to `(room, dst)`'s address. It keeps no other state.
- **Cost:** a 10-player match through the relay is at most ~10 x (12 KB/s lockstep + 2 KB/s voice) in and out: well under 1
  Mbit/s; the relay is only used by the pairs that cannot connect directly.

## 3. The game side

- **Transport** (`UdpTransport`): an optional relay (address, room, own id). Addresses of the form `relay:<id>` are virtual: a
  send to one goes to the relay with the header; a datagram arriving from the relay is unwrapped and presented as coming from
  `relay:<src>`. The session layer above sees ordinary addresses; nothing else changes (JOIN, PEERS, the match view, voice).
  Keepalive to the relay every 5 s keeps the NAT mapping open.
- **Session** (`UdpSession`): when the lobby service gives a room (hosting a public / code lobby, or looking a code up), the
  session enables the relay with a fresh id. JOIN carries the member's relay id; the PEERS table carries each member's relay id
  beside its address.
- **Fallback** (per peer): direct first. A peer from which nothing arrived for `kRelayAfterS` = 3 s while we have been sending
  (the JOIN, or session / lockstep traffic) is switched to its `relay:<id>` address, once, logged ("session: seat N via the
  relay"). A joiner adds the host's `relay:<id>` as its last join candidate after 3 s without JOIN_OK.
- **Voice** rides the same transport, so it follows the peer's path automatically.

## 4. Test (headless, no VPS)

`WFC_NET_DIRECT_DROP=1` (TEST ONLY) makes UdpTransport drop every non-relay datagram, in and out: the "blocked direct"
condition (two symmetric NATs) on loopback. The harness runs a local `lobby_service.py` (lobby + relay ports), a host that
registers, a member that joins by the 6-letter code, and the UDPMATCH lockstep match: the member must fall back to the relay,
seat, launch, and finish with equal SIMHASH every tick, voice frames delivered. A netshim profile cannot model it (netshim sits
between two known ports; the blocked case is about which endpoints exist at all), so the drop switch is the faithful harness.

## 5. Deploy

Nothing new to install: `deploy_lobby.sh` already ships `lobby_service.py`; the VPS firewall must open UDP 14010 **and** 14011.
The relay address the service hands out is its own public IP + the relay port (`--public-ip` when the VPS sits behind a NAT).
