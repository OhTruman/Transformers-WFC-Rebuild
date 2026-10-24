// Playtest 2026-10-23b P0 (#16 / #28 Scout cannot move, #31 respawn / QA restart): a private match (or Escalation) hosted alone
// launched as an ONLINE session: the session holds the host's own slot, so the "no remote human" branch cleared the seat order and
// fell through to the remotes path, where gl.online = !onlineSlots.empty() was true with 0 remotes. The World then ran the local
// player as a wire seat (moved only by lockstep commands) with no driver to send them: the pawn froze while the camera turned (the
// presentation interpolation flickering), and respawn / the QA restart, which go through the seat path, did nothing.
// Fix: online only when another human is in the match. Run from the tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/core/Application_Frontend.cpp', [
    ['    gl.online = !m.onlineSlots.empty();   // a session launch: the wire path for every human (offline: none)\n',
     '    // A session launch with another human: the wire path for every human. Hosted alone (the session holds only our own slot):\n' +
     '    // the local path, as the no-remote branch above intends (online with 0 remotes froze the local pawn: playtest 2026-10-23b P0).\n' +
     '    gl.online = !gl.remotes.empty();\n'],
]);
// SOLOLAUNCHTEST passed the broken launch (online true, 0 humans): it must be an offline launch outright.
ed('src/core/Application_Frontend.cpp', [
    ['    check(!(solo.online && !solo.humans.empty()), "alone: the launch starts no lockstep driver");\n',
     '    check(!solo.online && solo.remotes.empty(), "alone: an offline launch (no wire seat, no lockstep driver)");\n'],
]);
console.log('ok');
