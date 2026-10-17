# NETSHIM (netcode harness, tools/fidelity/NETCODE_HARNESS.md): a UDP impairment relay between game peers on loopback.
# Every ordered peer pair (i -> j) gets its own shim port; peer i sends to j through it, and the shim delivers to j's own port
# after the direction's impairment (latency + jitter, loss, burst loss, reorder, duplicate, timed outages). Decisions are
# deterministic per (seed, direction, packet index): the same seed drops / delays the same packets, so a failing row reruns
# identically. Every packet is logged (jsonl). Single thread, select() + a delivery heap (about 1 ms timing on Windows).
#   python netshim.py --peer A=127.0.0.1:5001 --peer B=127.0.0.1:5002 [--profile typical] [--dir A>B=bad] [--seed 1] \
#                     [--base-port 6000] [--log shim.jsonl] [--secs 0] [--map map.json]
#   prints / writes the port map: {"A>B": 6000, "B>A": 6001}: peer A sends to 127.0.0.1:6000 to reach B
import argparse, heapq, json, random, select, socket, sys, time, zlib

PROFILES = {   # one-way: latency ms, jitter ms (uniform +-), loss, reorder (extra delay 1-3 x jitter + 5 ms), duplicate,
               # burst (every s seconds drop n consecutive packets), outage (at t seconds for d seconds: 100 % loss)
    "lan":     dict(lat=0.5, jit=0.2, loss=0.0, reorder=0.0, dup=0.0),
    "good":    dict(lat=20, jit=3, loss=0.001, reorder=0.0, dup=0.0),
    "typical": dict(lat=50, jit=10, loss=0.01, reorder=0.0, dup=0.0),
    "bad":     dict(lat=100, jit=30, loss=0.05, reorder=0.02, dup=0.01),
    "burst":   dict(lat=50, jit=10, loss=0.01, reorder=0.0, dup=0.0, burst_every=10.0, burst_n=8),
    "outage":  dict(lat=50, jit=10, loss=0.01, reorder=0.0, dup=0.0, outage_at=20.0, outage_s=2.0),
    "slow":    dict(lat=120, jit=10, loss=0.01, reorder=0.0, dup=0.0),
}

def parse_profile(s):
    """a profile name, optionally with overrides: typical,lat=80,loss=0.02"""
    parts = s.split(","); p = dict(PROFILES[parts[0]])
    for kv in parts[1:]:
        k, v = kv.split("="); p[k] = float(v)
    return p

class Direction:
    def __init__(self, name, prof, seed, t0):
        self.name, self.p, self.t0 = name, prof, t0
        self.rng = random.Random(zlib.crc32(f"{seed}:{name}".encode()))
        self.idx = 0; self.burst_left = 0; self.next_burst = (prof["burst_every"] / 2) if prof.get("burst_every") else None   # (first burst at half the period, then every period)
        self.stats = dict(rx=0, dropped=0, dup=0, reordered=0, delivered=0)
    def decide(self, now):
        """-> list of delays (s) for this packet's copies; [] = dropped. Draws a fixed number of randoms per packet."""
        p = self.p; r = [self.rng.random() for _ in range(5)]; i = self.idx; self.idx += 1; t = now - self.t0
        self.stats["rx"] += 1
        if p.get("outage_at") is not None and p["outage_at"] <= t < p["outage_at"] + p.get("outage_s", 0):
            self.stats["dropped"] += 1; return [], "outage"
        if self.next_burst is not None and t >= self.next_burst:
            self.burst_left = int(p.get("burst_n", 0)); self.next_burst += p["burst_every"]
        if self.burst_left > 0:
            self.burst_left -= 1; self.stats["dropped"] += 1; return [], "burst"
        if r[0] < p["loss"]:
            self.stats["dropped"] += 1; return [], "loss"
        d = max(0.0, p["lat"] + (r[1] * 2 - 1) * p["jit"]) / 1000.0; why = "deliver"
        if r[2] < p["reorder"]:
            d += (p["jit"] * (1 + 2 * r[3]) + 5) / 1000.0; self.stats["reordered"] += 1; why = "reorder"
        out = [d]
        if r[4] < p["dup"]:
            out.append(d + max(p["jit"], 1) / 1000.0); self.stats["dup"] += 1; why += "+dup"
        return out, why

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--peer", action="append", required=True, help="NAME=host:port (the peer's own receive address)")
    ap.add_argument("--profile", default="lan"); ap.add_argument("--dir", action="append", default=[], help="A>B=profile[,k=v]")
    ap.add_argument("--seed", type=int, default=1); ap.add_argument("--base-port", type=int, default=6000)
    ap.add_argument("--log", default=None); ap.add_argument("--secs", type=float, default=0, help="0 = until killed")
    ap.add_argument("--map", default=None, help="write the port map JSON here")
    a = ap.parse_args()
    peers = {}
    for s in a.peer:
        n, hp = s.split("="); h, pt = hp.rsplit(":", 1); peers[n] = (h, int(pt))
    over = {d.split("=", 1)[0]: d.split("=", 1)[1] for d in a.dir}
    t0 = time.perf_counter(); socks = {}; dirs = {}; port = a.base_port; pmap = {}
    for i in peers:
        for j in peers:
            if i == j: continue
            k = f"{i}>{j}"; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", port)); s.setblocking(False)
            socks[s] = (k, j); dirs[k] = Direction(k, parse_profile(over.get(k, a.profile)), a.seed, t0); pmap[k] = port; port += 1
    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(json.dumps(pmap), flush=True)
    if a.map: json.dump({"ports": pmap, "peers": {k: f"{h}:{p}" for k, (h, p) in peers.items()}, "seed": a.seed,
                         "profiles": {k: d.p for k, d in dirs.items()}}, open(a.map, "w"), indent=1)
    log = open(a.log, "w") if a.log else None
    heap = []; seq = 0; end = t0 + a.secs if a.secs else None
    try:
        while end is None or time.perf_counter() < end:
            now = time.perf_counter()
            while heap and heap[0][0] <= now:
                _, _, data, dst, k = heapq.heappop(heap); out.sendto(data, peers[dst]); dirs[k].stats["delivered"] += 1
            wait = max(0.0, min(0.05, (heap[0][0] - now) if heap else 0.05))
            r, _, _ = select.select(list(socks), [], [], wait)
            for s in r:
                try: data, _src = s.recvfrom(65536)
                except (BlockingIOError, ConnectionResetError): continue
                k, dst = socks[s]; now = time.perf_counter(); ds, why = dirs[k].decide(now)
                for d in ds:
                    seq += 1; heapq.heappush(heap, (now + d, seq, data, dst, k))
                if log: log.write(json.dumps({"t": round(now - t0, 4), "dir": k, "i": dirs[k].idx - 1, "n": len(data), "what": why,
                                              "delay_ms": [round(d * 1000, 2) for d in ds]}) + "\n")
    except KeyboardInterrupt: pass
    finally:
        if log: log.close()
        print(json.dumps({k: d.stats for k, d in dirs.items()}), flush=True)

if __name__ == "__main__":
    main()
