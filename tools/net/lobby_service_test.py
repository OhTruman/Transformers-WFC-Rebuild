"""Loopback test of lobby_service.py (127.0.0.1 only): a host and a joiner socket against a service started here for 10 s.
  python -I lobby_service_test.py      -> one PASS / FAIL line per check, then SUMMARY"""
import json, os, socket, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SVC = ("127.0.0.1", 14011)
fails = 0


def check(ok, what):
    global fails
    print("LOBBYTEST %s: %s" % ("PASS" if ok else "FAIL", what), flush=True)
    fails += 0 if ok else 1


def sock():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    s.settimeout(1.0)
    return s


def ask(s, obj, want=None):
    s.sendto(b"WFCL" + json.dumps(obj).encode(), SVC)
    end = time.time() + 1.5
    while time.time() < end:
        try:
            d, _ = s.recvfrom(65536)
        except socket.timeout:
            continue
        if d.startswith(b"WFCL"):
            m = json.loads(d[4:])
            if want is None or m.get("op") == want:
                return m
    return None


proc = subprocess.Popen([sys.executable, "-I", os.path.join(HERE, "lobby_service.py"), "--bind", "%s:%d" % SVC, "--secs", "10"],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
try:
    time.sleep(0.5)
    host, joiner = sock(), sock()
    r = ask(host, {"op": "ping", "id": 1}, "pong")
    check(r and r.get("id") == 1 and r.get("seen") == "127.0.0.1:%d" % host.getsockname()[1], "ping: the service sees the sender's address")
    reg = {"op": "register", "name": "HostPC", "mode": "TDM", "playlist": "TeamDeathmatch", "map": 3, "players": 1, "max": 10,
           "version": "p2a", "region": "eu", "lan": "192.168.1.23:14000", "public": True, "state": "pregame",
           "vote": {"options": ["MP_IAC_Streets", "MP_IAC_Rust", "MP_UND_Gorge"], "counts": [0, 0, 0], "ends_in": 15}}
    r = ask(host, reg, "registered")
    code = r and r.get("code")
    check(bool(code) and len(code) == 6 and code.isalpha(), "register -> a 6-letter code (%s)" % code)
    r2 = ask(host, dict(reg, code=code), "registered")
    check(r2 and r2.get("code") == code, "re-register keeps the code")
    g = ask(joiner, {"op": "list", "version": "p2a"}, "games")
    check(g and len(g["games"]) == 1 and g["games"][0]["code"] == code and g["games"][0]["name"] == "HostPC", "list: the public lobby")
    g = ask(joiner, {"op": "list", "version": "other"}, "games")
    check(g is not None and g["games"] == [], "list: another version sees nothing")
    q = ask(joiner, {"op": "quick", "version": "p2a", "playlist": "TeamDeathmatch", "lan": "192.168.1.50:50000"}, "quick")
    check(q and len(q["candidates"]) == 1 and q["candidates"][0]["code"] == code and q["candidates"][0]["state"] == "pregame"
          and q["candidates"][0]["host_public"] == "127.0.0.1:%d" % host.getsockname()[1], "quick match: the candidate with its endpoints")
    pq = None
    end = time.time() + 1.5
    while time.time() < end and not pq:
        try:
            d, _ = host.recvfrom(65536)
            m = json.loads(d[4:])
            if m.get("op") == "punch":
                pq = m
        except socket.timeout:
            pass
    check(pq and pq.get("peer_lan") == "192.168.1.50:50000", "quick match: the candidate host gets a punch notice")
    e = ask(joiner, {"op": "lookup", "code": code.lower(), "lan": "192.168.1.50:50000"}, "endpoints")
    check(e and e.get("host_public") == "127.0.0.1:%d" % host.getsockname()[1] and e.get("host_lan") == "192.168.1.23:14000",
          "lookup: the host's public + LAN endpoints")
    p = None
    end = time.time() + 1.5
    while time.time() < end and not p:
        try:
            d, _ = host.recvfrom(65536)
            m = json.loads(d[4:])
            if m.get("op") == "punch":
                p = m
        except socket.timeout:
            pass
    check(p and p.get("peer_public") == "127.0.0.1:%d" % joiner.getsockname()[1] and p.get("peer_lan") == "192.168.1.50:50000",
          "the host is told to punch towards the joiner")
    joiner.sendto(b"WFCL" + json.dumps({"op": "relay", "to": "127.0.0.1:%d" % host.getsockname()[1], "data": "aGVsbG8="}).encode(), SVC)
    rl = None
    end = time.time() + 1.5
    while time.time() < end and not rl:
        try:
            d, _ = host.recvfrom(65536)
            m = json.loads(d[4:])
            if m.get("op") == "relayed":
                rl = m
        except socket.timeout:
            pass
    check(rl and rl.get("data") == "aGVsbG8=" and rl.get("from") == "127.0.0.1:%d" % joiner.getsockname()[1], "relay: joiner -> host")
    stranger = sock()
    stranger.sendto(b"WFCL" + json.dumps({"op": "relay", "to": "127.0.0.1:%d" % host.getsockname()[1], "data": "eA=="}).encode(), SVC)
    leak = None
    try:
        d, _ = host.recvfrom(65536)
        leak = json.loads(d[4:])
    except socket.timeout:
        pass
    check(leak is None, "relay refused for a sender that never looked a lobby up")
    # The binary relay (SYSTEMS_RELAY.md): register / lookup carry the room token + the relay port; two ids forward through it.
    room_hex, rport = r.get("room"), r.get("relay_port")
    check(bool(room_hex) and len(room_hex) == 16 and rport == SVC[1] + 1, "register: the relay room token + relay port (%s, %s)" % (room_hex, rport))
    e2 = ask(joiner, {"op": "lookup", "code": code}, "endpoints")
    check(e2 and e2.get("room") == room_hex and e2.get("relay_port") == rport, "lookup: the same room + relay port")
    RLY = ("127.0.0.1", rport or 0)
    room = int(room_hex or "0", 16)
    ra, rb = sock(), sock()
    def wr(s, src, dst, payload=b"", rm=None):
        s.sendto(b"WR" + struct.pack("<QII", room if rm is None else rm, src, dst) + payload, RLY)
    def recv(s):
        try:
            d, _ = s.recvfrom(65536)
            return d
        except socket.timeout:
            return None
    wr(ra, 11, 0); wr(rb, 22, 0)   # register both ids
    time.sleep(0.1)
    wr(rb, 22, 11, b"lockstep")
    got = recv(ra)
    check(got is not None and got[:2] == b"WR" and struct.unpack_from("<QII", got, 2) == (room, 22, 11) and got[18:] == b"lockstep",
          "relay: id 22 -> id 11 delivered whole (header names the sender)")
    wr(ra, 11, 22, b"voice")
    got = recv(rb)
    check(got is not None and got[18:] == b"voice", "relay: and back (11 -> 22)")
    rc = sock()
    rc.sendto(b"WR" + struct.pack("<QII", room ^ 1, 33, 11) + b"x", RLY)
    check(recv(ra) is None, "relay: a wrong room token is dropped")
    host.sendto(b"WFCL" + json.dumps({"op": "unregister", "code": code}).encode(), SVC)
    time.sleep(0.2)
    wr(rb, 22, 11, b"late")
    check(recv(ra) is None, "relay: the room ends with its lobby")
    host.sendto(b"WFCL" + json.dumps({"op": "unregister", "code": code}).encode(), SVC)
    time.sleep(0.2)
    g = ask(joiner, {"op": "list", "version": "p2a"}, "games")
    check(g is not None and g["games"] == [], "unregister removes the lobby")
finally:
    proc.kill()
    proc.wait()
print("LOBBYTEST SUMMARY: %s (%d failed)" % ("FAIL" if fails else "PASS", fails))
sys.exit(1 if fails else 0)
