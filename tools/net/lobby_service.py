"""WFC Rebuild lobby service (ONLINE_MATCHMAKING_DESIGN.md option A; user decision 2026-10-17: not deployed yet - the game points at it
by a setting later). One UDP port; the match itself stays lockstep peer-to-peer, this only helps the PCs find and reach each other.

  python lobby_service.py [--bind 0.0.0.0:14010] [--log FILE]       (Python 3.8+, standard library only)

Datagrams: b"WFCL" + one JSON object {"op": ..., ...}; every answer echoes "id" when the request had one.
  register   {name, mode, playlist, map, players, max, version, region, lan, public: bool, state: pregame | inprogress,
              vote: {options, counts, ends_in}, code?}
             -> {op: registered, code, seen}   (code: 6 letters, kept while the host re-registers every <= 5 s; gone after 15 s)
  unregister {code}
  list       {version, playlist?, mode?}      -> {op: games, games: [{code, name, mode, playlist, map, players, max, region}]}  (public only)
  quick      {version, mode?, playlist?, lan}  -> {op: quick, candidates: [{code, state, players, max, vote, host_public,
                                                 host_lan, ...}]}: Quick Match (no list on screen) - pregame lobbies first, then in-progress
                                                 matches with open seats, max 8; each candidate host gets a punch notice; the client pings
                                                 them and joins the best ping
  lookup     {code, lan?}                     -> {op: endpoints, code, host_public, host_lan}; the host gets
                                                 {op: punch, peer_public, peer_lan} at once (both sides then send to each other:
                                                 the UDP hole punch)
  relay      {to: "ip:port", data: base64}    -> forwards {op: relayed, from: "ip:port", data} to `to` if `to` is a registered host
                                                 or a peer that looked one up in the last 60 s (the relay fallback; small: lockstep
                                                 commands are ~5-15 KB/s per player)
  ping       {}                               -> {op: pong, seen}   (seen = the address the service sees for the sender)
  presence   {fp, name}                       -> {op: presence_ok}: this player (its fingerprint) is reachable at the sender's address;
                                                 re-sent every <= 20 s, gone 60 s after the last (the invite mailbox, decision D13)
  invite     {to: fp, from: fp, name, code}   -> {op: invite_sent, to, ok}; when `to` is present it gets {op: invited, from, name,
                                                 code} at once (a game / party code: the receiver joins it as any code)
  online     {fps: [fp, ...]}                 -> {op: online, fps: [the present ones]}   (the Friends List's Online text; max 64 asked)
No accounts, nothing stored on disk; lobbies live in memory.

Public-internet limits (a spoofed or flooding sender cannot grow memory or bounce large answers at a victim): each source IP gets a token
bucket (RATE_PER_S requests, RATE_BURST burst; over it the datagram is dropped silently), at most LOBBIES_PER_IP live lobbies per IP and
MAX_LOBBIES in all (a new register over a cap is refused: {op: registered, error: "full"}), a `list` answer carries at most LIST_MAX games.
The relay: at most RELAY_IDS_PER_ROOM ids per room, payloads up to RELAY_MAX_BYTES, RELAY_RATE_PER_S datagrams per second per source IP.

The binary relay (docs/handoff/SYSTEMS_RELAY.md; the fallback between two symmetric NATs), its own UDP port (--relay-port, default the
lobby port + 1): `registered`, `endpoints` and each `quick` candidate also carry `room` (16 hex digits: the lobby's relay token) and
`relay_port` (the client reaches the relay at the service's IP + this port). Datagram: b"WR" | room u64 | src u32 | dst u32 | payload
(little endian). The relay learns (room, src) -> the sender's address from every datagram and forwards the whole datagram to (room,
dst)'s address; dst 0 = register / keepalive. Unknown rooms are dropped; a room ends with its lobby; ids idle 60 s are forgotten.
"""
import argparse, asyncio, base64, json, random, struct, sys, time

MAGIC = b"WFCL"
B32 = "ABCDEFGHJKMNPQRSTVWXYZ"   # codes: letters only, no I L O U (easy to read aloud)
HOST_TTL, PEER_TTL, PRESENCE_TTL = 15.0, 60.0, 60.0
MAX_PRESENCE, PRESENCE_PER_IP = 65536, 64
RATE_PER_S, RATE_BURST = 50.0, 100.0          # lobby requests per source IP (a host re-registers every 5 s; generous: CGNAT shares one IP)
LOBBIES_PER_IP, MAX_LOBBIES, LIST_MAX = 16, 4096, 32
RELAY_IDS_PER_ROOM, RELAY_MAX_BYTES, RELAY_RATE_PER_S = 32, 1400, 1500.0   # a 16-player match through the relay stays far under these


class Bucket:
    """Per-IP token buckets (forgotten after a minute idle)."""
    def __init__(self, rate, burst):
        self.rate, self.burst, self.b = rate, burst, {}

    def take(self, ip, now):
        tok, last = self.b.get(ip, (self.burst, now))
        tok = min(self.burst, tok + (now - last) * self.rate)
        if len(self.b) > 65536:
            self.b = {k: v for k, v in self.b.items() if now - v[1] < 60.0}
        if tok < 1.0:
            self.b[ip] = (tok, now)
            return False
        self.b[ip] = (tok - 1.0, now)
        return True
RELAY_MAGIC, RELAY_HDR, RELAY_IDLE = b"WR", 2 + 8 + 4 + 4, 60.0


class Service(asyncio.DatagramProtocol):
    def __init__(self, log):
        self.lobbies = {}    # code -> dict(host=addr, info=..., last=t)
        self.peers = {}      # addr -> last lookup time (relay allow-list)
        self.presence = {}   # fingerprint -> [addr, name, last]   (the invite mailbox)
        self.log = log
        self.transport = None
        self.relay_port = 0  # set by main (0 = no relay)
        self.bucket = Bucket(RATE_PER_S, RATE_BURST)
        self.dropped = 0

    def connection_made(self, t):
        self.transport = t

    def send(self, addr, obj):
        self.transport.sendto(MAGIC + json.dumps(obj, separators=(",", ":")).encode(), addr)

    def note(self, *a):
        line = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
        print(line, flush=True)
        if self.log:
            self.log.write(line + "\n"); self.log.flush()

    def relay_fields(self, code):
        l = self.lobbies.get(code)
        return {"room": "%016x" % l["room"], "relay_port": self.relay_port} if l and self.relay_port else {}

    def room_alive(self, room):
        return any(l.get("room") == room for l in self.lobbies.values())

    def new_code(self):
        while True:
            c = "".join(random.choice(B32) for _ in range(6))
            if c not in self.lobbies:
                return c

    def expire(self):
        now = time.time()
        for c in [c for c, l in self.lobbies.items() if now - l["last"] > HOST_TTL]:
            self.note("expired", c)
            del self.lobbies[c]
        for a in [a for a, t in self.peers.items() if now - t > PEER_TTL]:
            del self.peers[a]
        for f in [f for f, v in self.presence.items() if now - v[2] > PRESENCE_TTL]:
            del self.presence[f]

    def datagram_received(self, data, addr):
        if not data.startswith(MAGIC) or len(data) > 4096:
            return
        if not self.bucket.take(addr[0], time.time()):
            self.dropped += 1
            if self.dropped % 1000 == 1:
                self.note("rate limit: dropping from", addr[0], "(%d dropped so far)" % self.dropped)
            return
        try:
            m = json.loads(data[4:].decode())
            assert isinstance(m, dict)
        except Exception:
            return
        self.expire()
        op, rid, seen = m.get("op"), m.get("id"), "%s:%d" % addr

        def reply(o):
            if rid is not None:
                o["id"] = rid
            self.send(addr, o)

        if op == "ping":
            reply({"op": "pong", "seen": seen})
        elif op == "presence":
            fp = str(m.get("fp", ""))[:64]
            if not fp:
                return
            if fp not in self.presence:
                if len(self.presence) >= MAX_PRESENCE or sum(1 for v in self.presence.values() if v[0][0] == addr[0]) >= PRESENCE_PER_IP:
                    return
            self.presence[fp] = [addr, str(m.get("name", ""))[:64], time.time()]
            reply({"op": "presence_ok"})
        elif op == "invite":
            to, frm = str(m.get("to", "")), str(m.get("from", ""))
            sender = self.presence.get(frm)
            if not sender or sender[0] != addr:   # only a present sender, from its own address (no forged 'from')
                reply({"op": "invite_sent", "to": to, "ok": False, "error": "not present"})
                return
            dst = self.presence.get(to)
            if dst:
                self.send(dst[0], {"op": "invited", "from": frm, "name": str(m.get("name", ""))[:64], "code": str(m.get("code", ""))[:32]})
                self.note("invite", frm, "->", to, m.get("code"))
            reply({"op": "invite_sent", "to": to, "ok": bool(dst)})
        elif op == "online":
            fps = [str(f) for f in (m.get("fps") or [])[:64]]
            reply({"op": "online", "fps": [f for f in fps if f in self.presence]})
        elif op == "register":
            code = m.get("code")
            if not code or code not in self.lobbies or self.lobbies[code]["host"] != addr:
                mine = sum(1 for l in self.lobbies.values() if l["host"][0] == addr[0])
                if mine >= LOBBIES_PER_IP or len(self.lobbies) >= MAX_LOBBIES:
                    reply({"op": "registered", "error": "full", "seen": seen})
                    return
                code = self.new_code()
                self.note("register", code, seen, m.get("name"), m.get("mode"), m.get("playlist"))
            info = {k: m.get(k) for k in ("name", "mode", "playlist", "map", "players", "max", "version", "region", "lan", "public",
                                           "state", "vote")}
            room = self.lobbies[code]["room"] if code in self.lobbies else random.getrandbits(64) or 1
            self.lobbies[code] = {"host": addr, "info": info, "last": time.time(), "room": room}
            reply(dict({"op": "registered", "code": code, "seen": seen}, **self.relay_fields(code)))
        elif op == "unregister":
            l = self.lobbies.get(m.get("code"))
            if l and l["host"] == addr:
                del self.lobbies[m["code"]]
                self.note("unregister", m["code"])
        elif op in ("list", "quick"):
            games = []
            for c, l in self.lobbies.items():
                i = l["info"]
                if not i.get("public") or i.get("version") != m.get("version"):
                    continue
                if m.get("playlist") and i.get("playlist") != m.get("playlist"):
                    continue
                if m.get("mode") and i.get("mode") != m.get("mode"):
                    continue
                if (i.get("players") or 0) >= (i.get("max") or 0):
                    continue
                games.append(dict({"code": c}, **{k: i.get(k) for k in ("name", "mode", "playlist", "map", "players", "max", "region",
                                                                      "state", "vote")}))
            if op == "list":
                reply({"op": "games", "games": games[:LIST_MAX]})
            else:   # Quick Match: candidates (pregame first, then in progress; the fullest first), max 8, + a punch notice to each
                games.sort(key=lambda g: (g.get("state") != "pregame", -(g.get("players") or 0)))
                games = games[:8]
                for g in games:
                    h = self.lobbies[g["code"]]["host"]
                    g["host_public"] = "%s:%d" % h
                    g["host_lan"] = self.lobbies[g["code"]]["info"].get("lan")
                    g.update(self.relay_fields(g["code"]))
                    self.send(h, {"op": "punch", "peer_public": seen, "peer_lan": m.get("lan")})
                self.peers[addr] = time.time()
                reply({"op": "quick", "candidates": games})
        elif op == "lookup":
            l = self.lobbies.get(str(m.get("code", "")).upper())
            if not l:
                reply({"op": "endpoints", "code": m.get("code"), "error": "unknown"})
                return
            host = "%s:%d" % l["host"]
            reply(dict({"op": "endpoints", "code": m.get("code"), "host_public": host, "host_lan": l["info"].get("lan")},
                       **self.relay_fields(str(m.get("code", "")).upper())))
            self.send(l["host"], {"op": "punch", "peer_public": seen, "peer_lan": m.get("lan")})
            self.peers[addr] = time.time()
            self.note("lookup", m.get("code"), "by", seen, "-> host", host)
        elif op == "relay":
            to = m.get("to", "")
            try:
                ip, port = to.rsplit(":", 1)
                dst = (ip, int(port))
            except Exception:
                return
            allowed = any(l["host"] == dst for l in self.lobbies.values()) or dst in self.peers
            sender_ok = any(l["host"] == addr for l in self.lobbies.values()) or addr in self.peers
            if allowed and sender_ok:
                self.send(dst, {"op": "relayed", "from": seen, "data": m.get("data", "")})


class Relay(asyncio.DatagramProtocol):
    """The binary relay: forwards b"WR" datagrams between the ids of one room (see the module doc)."""
    def __init__(self, svc):
        self.svc = svc
        self.ids = {}        # (room, id) -> [addr, last seen]
        self.transport = None
        self.forwarded = 0
        self.bucket = Bucket(RELAY_RATE_PER_S, RELAY_RATE_PER_S)

    def connection_made(self, t):
        self.transport = t

    def datagram_received(self, data, addr):
        if len(data) < RELAY_HDR or len(data) > RELAY_HDR + RELAY_MAX_BYTES or not data.startswith(RELAY_MAGIC):
            return
        now = time.time()
        if not self.bucket.take(addr[0], now):
            return
        room, src, dst = struct.unpack_from("<QII", data, 2)
        if (room, src) not in self.ids:
            if not self.svc.room_alive(room):
                return   # only clients the service gave the token to
            if src and sum(1 for k in self.ids if k[0] == room) >= RELAY_IDS_PER_ROOM:
                return   # a full room
        if src:
            self.ids[(room, src)] = [addr, now]
        if dst:
            d = self.ids.get((room, dst))
            if d and now - d[1] <= RELAY_IDLE and self.svc.room_alive(room):
                self.transport.sendto(data, d[0])
                self.forwarded += 1
        if random.random() < 0.01:   # forget idle ids now and then
            for k in [k for k, v in self.ids.items() if now - v[1] > RELAY_IDLE]:
                del self.ids[k]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bind", default="0.0.0.0:14010")
    ap.add_argument("--log", default=None)
    ap.add_argument("--relay-port", type=int, default=-1, help="the binary relay's UDP port (-1 = the lobby port + 1, 0 = no relay)")
    ap.add_argument("--secs", type=float, default=0, help="stop after this long (tests); 0 = run until killed")
    a = ap.parse_args()
    host, port = a.bind.rsplit(":", 1)
    log = open(a.log, "a", encoding="utf-8") if a.log else None
    loop = asyncio.new_event_loop()
    svc = Service(log)
    t, _ = loop.run_until_complete(loop.create_datagram_endpoint(lambda: svc, local_addr=(host, int(port))))
    svc.note("lobby service on", a.bind)
    rt = None
    rport = int(port) + 1 if a.relay_port < 0 else a.relay_port
    if rport:
        relay = Relay(svc)
        rt, _ = loop.run_until_complete(loop.create_datagram_endpoint(lambda: relay, local_addr=(host, rport)))
        svc.relay_port = rport
        svc.note("relay on %s:%d" % (host, rport))
    try:
        if a.secs:
            loop.run_until_complete(asyncio.sleep(a.secs))
        else:
            loop.run_forever()
    except KeyboardInterrupt:
        pass
    t.close()
    if rt:
        rt.close()


if __name__ == "__main__":
    main()
