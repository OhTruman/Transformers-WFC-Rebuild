# netshim self-test against a UDP echo (no game exe): peer A sends N packets at 60 Hz (seq + send time) to B through the shim; B
# echoes each back through the shim. Per profile: the measured one-way delay (RTT / 2) vs the profile, the loss rate per
# direction vs the profile, reorders, duplicates; and determinism: two runs with the same seed drop the same packet indices.
#   python netshim_test.py [--profiles lan,typical,bad,burst] [--n 600] [--out dir]
import argparse, json, os, socket, statistics, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); PY = sys.executable

def run(profile, n, seed, out, base):
    pa, pb = base, base + 1
    log = os.path.join(out, f"shim_{profile}_{seed}.jsonl")
    shim = subprocess.Popen([PY, "-I", os.path.join(HERE, "netshim.py"), "--peer", f"A=127.0.0.1:{pa}", "--peer", f"B=127.0.0.1:{pb}",
                             "--profile", profile, "--seed", str(seed), "--base-port", str(base + 10), "--log", log],
                            stdout=subprocess.PIPE, text=True)
    pmap = json.loads(shim.stdout.readline())
    A = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); A.bind(("127.0.0.1", pa)); A.setblocking(False)
    B = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); B.bind(("127.0.0.1", pb)); B.setblocking(False)
    a2b, b2a = ("127.0.0.1", pmap["A>B"]), ("127.0.0.1", pmap["B>A"])
    sent = {}; got_b = []; got_a = []; t0 = time.perf_counter(); k = 0
    while True:
        now = time.perf_counter()
        if k < n and now >= t0 + k / 60.0:
            A.sendto(f"{k} {now:.6f}".encode(), a2b); sent[k] = now; k += 1
        for s, sink in ((B, got_b), (A, got_a)):
            try:
                while True:
                    d, _ = s.recvfrom(2048); sink.append((time.perf_counter(), d.decode()))
                    if s is B: B.sendto(d, b2a)
            except (BlockingIOError, ConnectionResetError): pass
        if k >= n and now > t0 + n / 60.0 + 1.5: break
        time.sleep(0.0005)
    shim.terminate(); stats = shim.communicate(timeout=10)[0].strip().splitlines()
    rtt = [(t - sent[int(m.split()[0])]) * 1000 for t, m in got_a]
    seq_b = [int(m.split()[0]) for _, m in got_b]
    reorders = sum(1 for x, y in zip(seq_b, seq_b[1:]) if y < x); dups = len(seq_b) - len(set(seq_b))
    drops = sorted(set(range(n)) - set(seq_b))
    return dict(profile=profile, seed=seed, sent=n, b_got=len(seq_b), a_got=len(got_a), loss_ab=round(1 - len(set(seq_b)) / n, 4),
                rtt_p50=round(statistics.median(rtt), 2) if rtt else None, rtt_p95=round(sorted(rtt)[int(0.95 * len(rtt)) - 1], 2) if rtt else None,
                reorders=reorders, dups=dups, drops=drops, shim_stats=stats[-1] if stats else "")

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--profiles", default="lan,typical,bad,burst"); ap.add_argument("--n", type=int, default=600)
    ap.add_argument("--out", default=os.path.join("work", "netshim")); a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    sys.path.insert(0, HERE); import netshim
    rows = []; base = 15000; ok_all = True
    for i, pr in enumerate(a.profiles.split(",")):
        p = netshim.PROFILES[pr]
        r1 = run(pr, a.n, 7, a.out, base + 100 * i); r2 = run(pr, a.n, 7, a.out, base + 100 * i + 50)
        exp_rtt = 2 * p["lat"]; tol = max(3.0, 0.15 * exp_rtt) + 2 * p["jit"]
        ok_rtt = r1["rtt_p50"] is not None and abs(r1["rtt_p50"] - exp_rtt) <= tol
        nb = int((a.n / 60.0 + p["burst_every"] / 2) // p["burst_every"]) if p.get("burst_every") else 0; exp_loss = p["loss"] + nb * p.get("burst_n", 0) / a.n; T = a.n / 60.0
        if p.get("outage_at") is not None: exp_loss += max(0.0, min(T, p["outage_at"] + p["outage_s"]) - p["outage_at"]) * 60.0 / a.n * (1 - p["loss"])
        ok_loss = abs(r1["loss_ab"] - exp_loss) <= max(0.01, 3 * (exp_loss * (1 - exp_loss) / a.n) ** 0.5)
        ok_det = r1["drops"] == r2["drops"]
        ok = ok_rtt and ok_loss and ok_det; ok_all &= ok
        rows.append(r1)
        print(f"{'PASS' if ok else 'FAIL'} {pr}: RTT p50 {r1['rtt_p50']} ms (expected {exp_rtt:.1f} +- {tol:.1f}), p95 {r1['rtt_p95']}; "
              f"loss A>B {r1['loss_ab']:.3f} (expected {exp_loss:.3f}); reorders {r1['reorders']}, dups {r1['dups']}; "
              f"same drops with the same seed: {ok_det} ({len(r1['drops'])} vs {len(r2['drops'])})")
    json.dump(rows, open(os.path.join(a.out, "netshim_test.json"), "w"), indent=1)
    sys.exit(0 if ok_all else 1)

if __name__ == "__main__":
    main()
