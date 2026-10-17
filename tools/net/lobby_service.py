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
No accounts, nothing stored on disk; lobbies live in memory.
"""
import argparse, asyncio, base64, json, random, sys, time

MAGIC = b"WFCL"
B32 = "ABCDEFGHJKMNPQRSTVWXYZ"   # codes: letters only, no I L O U (easy to read aloud)
HOST_TTL, PEER_TTL = 15.0, 60.0


class Service(asyncio.DatagramProtocol):
    def __init__(self, log):
        self.lobbies = {}    # code -> dict(host=addr, info=..., last=t)
        self.peers = {}      # addr -> last lookup time (relay allow-list)
        self.log = log
        self.transport = None

    def connection_made(self, t):
        self.transport = t

    def send(self, addr, obj):
        self.transport.sendto(MAGIC + json.dumps(obj, separators=(",", ":")).encode(), addr)

    def note(self, *a):
        line = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
        print(line, flush=True)
        if self.log:
            self.log.write(line + "\n"); self.log.flush()

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

    def datagram_received(self, data, addr):
        if not data.startswith(MAGIC):
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
        elif op == "register":
            code = m.get("code")
            if not code or code not in self.lobbies or self.lobbies[code]["host"] != addr:
                code = self.new_code()
                self.note("register", code, seen, m.get("name"), m.get("mode"), m.get("playlist"))
            info = {k: m.get(k) for k in ("name", "mode", "playlist", "map", "players", "max", "version", "region", "lan", "public",
                                           "state", "vote")}
            self.lobbies[code] = {"host": addr, "info": info, "last": time.time()}
            reply({"op": "registered", "code": code, "seen": seen})
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
                reply({"op": "games", "games": games})
            else:   # Quick Match: candidates (pregame first, then in progress; the fullest first), max 8, + a punch notice to each
                games.sort(key=lambda g: (g.get("state") != "pregame", -(g.get("players") or 0)))
                games = games[:8]
                for g in games:
                    h = self.lobbies[g["code"]]["host"]
                    g["host_public"] = "%s:%d" % h
                    g["host_lan"] = self.lobbies[g["code"]]["info"].get("lan")
                    self.send(h, {"op": "punch", "peer_public": seen, "peer_lan": m.get("lan")})
                self.peers[addr] = time.time()
                reply({"op": "quick", "candidates": games})
        elif op == "lookup":
            l = self.lobbies.get(str(m.get("code", "")).upper())
            if not l:
                reply({"op": "endpoints", "code": m.get("code"), "error": "unknown"})
                return
            host = "%s:%d" % l["host"]
            reply({"op": "endpoints", "code": m.get("code"), "host_public": host, "host_lan": l["info"].get("lan")})
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bind", default="0.0.0.0:14010")
    ap.add_argument("--log", default=None)
    ap.add_argument("--secs", type=float, default=0, help="stop after this long (tests); 0 = run until killed")
    a = ap.parse_args()
    host, port = a.bind.rsplit(":", 1)
    log = open(a.log, "a", encoding="utf-8") if a.log else None
    loop = asyncio.new_event_loop()
    svc = Service(log)
    t, _ = loop.run_until_complete(loop.create_datagram_endpoint(lambda: svc, local_addr=(host, int(port))))
    svc.note("lobby service on", a.bind)
    try:
        if a.secs:
            loop.run_until_complete(asyncio.sleep(a.secs))
        else:
            loop.run_forever()
    except KeyboardInterrupt:
        pass
    t.close()


if __name__ == "__main__":
    main()
