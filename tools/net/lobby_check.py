"""Health check of a deployed lobby service, run from any PC:  python lobby_check.py <server-ip>[:<port>]

Checks (each one line, PASS / FAIL; exit 0 = all pass):
  1. the lobby port answers a ping (and shows the address it sees for this PC);
  2. a private test lobby registers (code + relay token) and a lookup of that code finds it;
  3. the relay port forwards a datagram between two ids of that lobby's room (both from this PC, two sockets);
  4. the test lobby unregisters.
Nothing is left behind on the server. Standard library only (Python 3.8+).
"""
import json, socket, struct, sys, time

MAGIC = b"WFCL"


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: lobby_check.py <server-ip>[:<port>]")
    host, _, port = sys.argv[1].partition(":")
    port = int(port or 14010)
    fails = 0

    def check(ok, what):
        nonlocal fails
        fails += 0 if ok else 1
        print("LOBBYCHECK %s %s" % ("PASS" if ok else "FAIL", what))
        return ok

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(2.0)

    def ask(o, sock=s):
        sock.sendto(MAGIC + json.dumps(o).encode(), (host, port))
        try:
            while True:
                d = sock.recv(4096)
                if d.startswith(MAGIC):
                    m = json.loads(d[4:])
                    if m.get("op") != "punch":
                        return m
        except socket.timeout:
            return None

    pong = ask({"op": "ping"})
    if not check(bool(pong and pong.get("op") == "pong"), "lobby port %s:%d answers (it sees this PC as %s)"
                 % (host, port, pong.get("seen") if pong else "-")):
        sys.exit(1)
    reg = ask({"op": "register", "name": "lobby_check", "public": False, "version": "check", "players": 1, "max": 2})
    code = reg.get("code") if reg else None
    check(bool(code) and "room" in reg, "a private test lobby registers (code %s, relay port %s)" % (code, reg.get("relay_port") if reg else "-"))
    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.settimeout(2.0)
    look = ask({"op": "lookup", "code": code}, peer) if code else None
    check(bool(look and look.get("host_public")), "a lookup of %s finds the host at %s" % (code, look.get("host_public") if look else "-"))
    if reg and reg.get("room") and reg.get("relay_port"):
        room, rport = int(reg["room"], 16), int(reg["relay_port"])
        a, b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM), socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        b.settimeout(2.0)
        hdr = lambda src, dst: b"WR" + struct.pack("<QII", room, src, dst)
        b.sendto(hdr(2, 0), (host, rport))          # b registers as id 2
        time.sleep(0.3)
        a.sendto(hdr(1, 2) + b"hello", (host, rport))
        try:
            got = b.recv(2048)
        except socket.timeout:
            got = b""
        check(got.endswith(b"hello"), "the relay (UDP %d) forwards a datagram between two ids of the room" % rport)
    else:
        check(False, "the relay: no room / relay port in the registration")
    if code:
        s.sendto(MAGIC + json.dumps({"op": "unregister", "code": code}).encode(), (host, port))
        time.sleep(0.3)
        gone = ask({"op": "lookup", "code": code}, peer)
        check(bool(gone and gone.get("error") == "unknown"), "the test lobby unregisters")
    print("LOBBYCHECK SUMMARY: %s" % ("PASS" if not fails else "%d FAILED" % fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
