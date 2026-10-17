# Online matchmaking: private codes and public matches (Systems design, 2026-10-17)

The user wants two ways into a match: **private** (the host copies a game code, friends paste it) and **public** ("find a public
match"). In both, **the match itself stays peer-to-peer lockstep** (phase 1: every PC simulates the whole match from the players'
commands); matchmaking only gets the PCs talking to each other. Fits ONLINE_MP_ARCHITECTURE.md and ONLINE_PHASE2_SPLIT.md.

## What has to be solved

1. **Discovery:** a PC must learn the other PCs' addresses (a code, or a list of public lobbies).
2. **Reachability:** most home PCs sit behind a router (NAT): an unsolicited packet from the internet is dropped, so two home PCs
   cannot simply send to each other's public address. On a LAN this does not apply (phase 2 starts LAN-only: the code is the LAN
   address).
3. **Fallback:** ~10-20 % of home connections (strict / symmetric NAT, carrier-grade NAT, some mobile and university networks)
   cannot be punched through; their packets must go through a relay.

## Option A - a small central lobby service (recommended)

One small program (ours, C++ on the same net code, or Python for speed of writing; ~1,000-1,500 lines) on one always-on machine:

| job | how |
|---|---|
| **Register a lobby** | the host sends {code, mode, map, players / max, region, version, voice on} every 5 s while open; gone after 15 s silent |
| **Private code** | the service issues a short code (6 letters, no 0/O/1/I); joining by code returns the host's addresses - no public listing |
| **Public list + quick match** | list filtered by version / mode / region, sorted by fill and ping; "find a public match" joins the best open lobby, or hosts one if none fits |
| **NAT punch assist** | each PC learns its public address from the service (it sees where the packets come from); on join the service tells both sides the other's public + LAN address and a moment to start; both send at once, which opens most routers (UDP hole punching) |
| **Relay fallback** | if punching fails within ~3 s, that pair's packets go through the service (it forwards UDP by member id); lockstep commands are small (~5-15 KB/s per player), so one box relays dozens of players |
| **Voice star relay** | the phase 1 voice star (clients send to the host, the host fans out) can run on the service instead when the host's upload is small: ~3 KB/s per talker in, x listeners out |

**What the user provides to run it:**

- **A machine that is always on with a public IP:** the cheapest is a small VPS (1 vCPU, 1 GB RAM, ~1 TB traffic/month; about
  **$4-6 per month** at the usual providers), or an always-on home PC with **one UDP port forwarded** on the router (free, but the
  home upload limits the relay).
- **Ports:** one UDP port (e.g. 14000) for lobby + punching + relay; optionally one TCP port for a plain web status page.
- **A name:** the address goes into the game's settings (or a DNS name the user owns); nothing is hard-wired to a company.
- **Upkeep:** start-on-boot service; logs; no accounts or personal data stored (lobbies are in memory and vanish when closed).

Rough load: 100 players at once in 10 matches, worst case all relayed: ~1.5 MB/s through the box (most pairs punch directly and
use none) - within a $5 VPS.

## Option B - pure peer-to-peer discovery

| way | works for | gaps |
|---|---|---|
| LAN broadcast (UDP 255.255.255.255 / multicast) | same home network: "find a LAN match" | not across the internet |
| Codes that encode the host's public address | friends, if the host forwards a port on the router | every host must configure their router; no NAT punching without a third party to coordinate it; no relay |
| A DHT (BitTorrent-style) for public lobbies | public lists without our server | still needs bootstrap nodes (someone's always-on machines), still cannot punch or relay, slow and fiddly; not worth it for one game |

## Recommendation

**Option A**, built in this order:

1. **LAN first (no server):** Host shows its LAN address + code; Join by code / IP; plus a **"find a LAN match"** broadcast. This is
   ONLINE_PHASE2_SPLIT.md item 1 and needs nothing from the user.
2. **The lobby service** (Systems, ~8-10 h): register / list / codes / quick match + punch assist, our own small program, runnable
   on a VPS or a home PC. Private codes and the public list both come from it.
3. **Relay fallback + the voice relay** on the same service (~4 h), so strict-NAT players can still play.

Pure P2P alone cannot give internet public matches or reliable joining across routers; a ~$5/month box (or one forwarded port on an
always-on PC) can. Without the service configured, the game still offers LAN play and direct-IP joining.

## Decisions for the user

- **Run the service on a VPS (~$4-6 / month) or an always-on home PC with one UDP port forwarded?**
- **Public lobbies open to anyone with the game, or only to people who have the service address (a "friends' server")?**
- **Regions:** one service for everyone at first (simplest), or region tags for later?
