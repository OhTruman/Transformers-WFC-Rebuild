# Online matchmaking: private codes, Quick Match by ping, map voting (Systems design, 2026-10-17)

User decisions (2026-10-17): peer-to-peer now, built so a server drops in later; a **Hetzner VPS is approved** (LOBBY_VPS_SETUP.md);
**public play has no server list**: Quick Match for a chosen mode joins the best-ping match - a pregame lobby that is gathering or an
in-progress match with open seats; the pregame lobby has **Halo Reach-style map voting**. In every case **the match itself stays
lockstep peer-to-peer** between the players' PCs; matchmaking only gets them talking.

## What works with what

| way in | needs the lobby service? | how |
|---|---|---|
| Private match, same home network | no | the host's LAN code (2 groups) or its LAN address |
| Private match, across the internet | no | the host's internet code (4 groups: public + LAN endpoint) from its router's UPnP mapping, else STUN; reaches the host when the router mapped the port (most home routers) |
| Private match, a strict router | yes | the 6-letter service code: the service tells both PCs to send to each other at once (hole punch), else relays |
| Quick Match | yes (no LAN fallback across the internet) | below; without the service: LAN only ("find a LAN match" by UDP broadcast) |

## Quick Match by ping (no list)

1. **Hosts register** with the service every 5 s while open: {mode, playlist, state = pregame / in progress, seats open / max,
   map vote state (pregame), version, region, the host's LAN endpoint}. The service records the address it sees (the public endpoint).
2. **The player picks a mode** and presses Quick Match; the client asks the service for **candidates** (`quick`): every open match
   of that mode / version with a free seat - pregame lobbies first, then in-progress matches with open seats - at most 8, the
   fullest first.
3. **The client pings each candidate** directly (one tiny datagram, the host answers; the service's punch notice goes to each host so
   the replies get through): 300 ms per round, 3 rounds. It drops candidates over 250 ms and **joins the best ping** (ties: the
   pregame lobby, then the fuller one). Progress = the original "Reserving space in game `i of `n".
4. **None fits** -> the player **hosts a pregame lobby** for that mode (registered public), and later quick-matchers find it.
5. **Joining in progress**: the member waits in the lobby with "Match in progress" (decision D6) and takes the next open seat at the
   host's agreed tick (the session's slot swap: a bot hands its seat over; ONLINE_PHASE2_SPLIT.md).

## Pregame lobby: Halo Reach-style map voting

- When the lobby has enough players (or the host starts it), the host **draws 3 options** from the mode's playlist (map, and the
  mode variant where the playlist has several), plus **"None of the above"** (once per lobby: a fresh 3).
- **Everyone votes** (one vote each, changeable) for **15 s**; the most votes wins (ties: random among the tied, the host's seed).
- The vote state travels **in the session like LobbyState**: the host owns it and publishes `vote {options[], counts[], endsAt,
  myVote?}` to the members; each member sends its vote to the host; the host also registers the state with the service (so a
  quick-matcher joining mid-vote sees it and can vote).
- The **winner loads**: the host's launch record (URL + seed + roster) as today, then the load gate and the match.
- Public lobby rules on the Frontend side (Frontend, ONLINE_LOBBY_FLOW_V2.md 4): the original 45 s autostart once enough players are
  in, Kick Idle default on, the playlist fixes mode / limits.

## The service (tools/net/lobby_service.py; one UDP port, 14010)

| op | from | does |
|---|---|---|
| register {mode, playlist, state, open, max, vote, version, region, lan, public} | host, every 5 s | -> code (6 letters); gone 15 s after the last |
| quick {mode, version, region?} | client | -> candidates [{code, state, open, max, vote}] + a punch notice to each candidate host |
| lookup {code} | client (private 6-letter code) | -> the host's public + LAN endpoint; the host gets a punch notice |
| relay {to, data} | either side | forwards between a host and a client that looked it up (the strict-router fallback, and the voice relay) |
| ping | anyone | -> the address the service sees (the client's public endpoint) |

No accounts; lobbies live in memory; the log holds codes and addresses only.

## Built in this order

1. **Now (no server needed):** LAN + internet codes, direct UDP, UPnP / STUN (UdpSession, done in code); LAN discovery broadcast.
2. **The service on the VPS** (deploy script ready; the user's IP + go): register / quick / lookup / punch / relay.
3. **Quick Match by ping + map voting** in UdpSession (the client side of `quick`, the ping round, the vote messages) and in the
   Frontend (the vote screen; the Quick Match button per mode).

## Decisions for the user (open)

- Voting: 3 options + "None of the above" once, 15 s - or another count / time?
- Joining in progress: allowed for every mode, or pregame only for some (e.g. Capture the Flag)?
- Region tags (one Ashburn server for everyone first; region only matters for the ping choice, which already picks the nearest host).
