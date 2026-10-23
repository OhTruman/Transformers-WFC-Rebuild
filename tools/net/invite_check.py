import json, socket, subprocess, sys, time
svc = subprocess.Popen([sys.executable, '-I', 'F:/Transformers Rebuild/Rebuild-Systems/tools/net/lobby_service.py', '--bind', '127.0.0.1:24176', '--secs', '15'],
                       stdout=subprocess.DEVNULL)
time.sleep(0.8)
A, B, C = (socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(3))
for s in (A, B, C): s.settimeout(0.5)
def send(s, o): s.sendto(b'WFCL' + json.dumps(o).encode(), ('127.0.0.1', 24176))
def recv(s):
    try: return json.loads(s.recv(4096)[4:])
    except socket.timeout: return None
ok = True
def check(c, what):
    global ok
    ok &= bool(c); print('INVITECHECK', 'PASS' if c else 'FAIL', what)
send(A, {'op': 'presence', 'fp': 'WFC-AAAA', 'name': 'Alice'}); check(recv(A) == {'op': 'presence_ok'}, 'A present')
send(B, {'op': 'presence', 'fp': 'WFC-BBBB', 'name': 'Bob'}); check(recv(B) == {'op': 'presence_ok'}, 'B present')
send(A, {'op': 'invite', 'to': 'WFC-BBBB', 'from': 'WFC-AAAA', 'name': 'Alice', 'code': 'QWERTY'})
got = recv(B); check(got and got.get('op') == 'invited' and got.get('code') == 'QWERTY' and got.get('from') == 'WFC-AAAA', 'B receives the invite: %s' % got)
r = recv(A); check(r and r.get('ok') is True, 'A: invite_sent ok')
send(A, {'op': 'invite', 'to': 'WFC-ZZZZ', 'from': 'WFC-AAAA', 'code': 'QWERTY'}); r = recv(A); check(r and r.get('ok') is False, 'an offline receiver: ok false')
send(C, {'op': 'invite', 'to': 'WFC-BBBB', 'from': 'WFC-AAAA', 'code': 'EVILXX'}); r = recv(C)
check(r and r.get('ok') is False and recv(B) is None, 'a forged sender (another address) is refused, B gets nothing')
send(C, {'op': 'online', 'fps': ['WFC-AAAA', 'WFC-ZZZZ', 'WFC-BBBB']}); r = recv(C); check(r and r.get('fps') == ['WFC-AAAA', 'WFC-BBBB'], 'online: %s' % r)
svc.kill(); svc.wait()
print('INVITECHECK SUMMARY:', 'PASS' if ok else 'FAIL')
