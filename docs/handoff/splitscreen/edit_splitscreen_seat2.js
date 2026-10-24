// Split screen part 1 (Gameplay): a second local human seat on the remote-seat path (WorldLocalSeat.cpp copied from argv[2]) +
// the hooks: seat 2's controller input per frame, its command per step, its camera collision, the World API (addLocalSeat /
// removeLocalSeat / setSeat2Input / localSeatCount / seatPlayer / seatCamera), WFC_SEAT2BOT (test only) joining at the match start.
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
fs.copyFileSync(path.join(G, 'docs/handoff/splitscreen/WorldLocalSeat.cpp'), 'src/game/WorldLocalSeat.cpp');
ed('src/game/PlayerController.h', [
    ['    void setWireSeat(bool w) { wireSeat_ = w; }\n',
     '    void setWireSeat(bool w) { wireSeat_ = w; }\n' +
     '    // A controller that never steps its pawn (split-screen seat 2) takes the collision worlds here (applyToPawn sets them otherwise).\n' +
     '    void setWorlds(const CollisionWorld* col, const CollisionWorld* ray) { col_ = col; colRay_ = ray; }\n'],
]);
ed('src/game/World.h', [
    ['    RemoteCommand encodeLocalCommand();\n',
     '    RemoteCommand encodeLocalCommand();\n' +
     '    // Split screen: a second local human seat (a Remote participant fed by its own local controller; GAMEPLAY_SPLITSCREEN_PLAN.md).\n' +
     '    int addLocalSeat(int pad, const CharacterSelection& sel, bool selected);   // the seat\'s match player (-1: none)\n' +
     '    void removeLocalSeat();\n' +
     '    void setSeat2Input(const platform::InputFrame& in);   // pad 2\'s frame, before handleInput\n' +
     '    int localSeatCount() const;\n' +
     '    int localSeatPlayer(int seat) const;   // split seat 0 / 1 -> its match player\n' +
     '    void seatCamera(int seat, render::Camera& cam) const;\n' +
     '    RemoteCommand encodeSeatCommand(PlayerController& ctl, Character* pawn, int p, bool dead);\n'],
    ['    Player player_;\n',
     '    Player player_;\n' +
     '    int seat2_ = -1, seat2Pad_ = -1;          // split screen: the second local seat\'s match player, its pad\n' +
     '    PlayerController seat2Ctl_;                // its camera / aim / intent (wire seat: commands only)\n' +
     '    platform::InputFrame seat2In_;\n' +
     '    void seat2HandleInput(float dt);\n' +
     '    void seat2QueueCommand();\n'],
]);
let w = fs.readFileSync('src/game/World.cpp', 'utf8');
const crlf = w.includes('\r\n'); w = w.replace(/\r\n/g, '\n');
// encodeSeatCommand: encodeLocalCommand's body for any controller / pawn / seat (encodeLocalCommand itself unchanged: identity)
const i0 = w.indexOf('RemoteCommand World::encodeLocalCommand() {');
const i1 = w.indexOf('\n}\n', i0) + 3;
if (i0 < 0 || i1 < 3) throw new Error('encodeLocalCommand not found');
let body = w.slice(i0, i1);
let seat = body.replace('RemoteCommand World::encodeLocalCommand() {\n    PlayerController& ctl = player_.controller();\n',
                        'RemoteCommand World::encodeSeatCommand(PlayerController& ctl, Character* pawnp, int p, bool dead) {\n    Character& pawn = *pawnp;\n');
if (seat === body) throw new Error('encodeLocalCommand head changed');
seat = seat.split('seatMissile(localPlayer_)').join('seatMissile(p)').split('localDead_').join('dead').split('player_.pawn()').join('pawn');
w = w.slice(0, i1) + '\n// Split screen: the same encoding for seat 2\'s controller and pawn.\n' + seat + w.slice(i1);
const ed2 = (a, b) => { if (w.split(a).length !== 2) throw new Error('World.cpp anchor: ' + a.slice(0, 80)); w = w.replace(a, b); };
ed2('    player_.controller().handleInput(cp_.active && cp_.cinematicMode ? platform::InputFrame{} : in, dt);',
    '    player_.controller().handleInput(cp_.active && cp_.cinematicMode ? platform::InputFrame{} : in, dt);\n    seat2HandleInput(dt);   // split screen: the second local seat\'s controller');
ed2('    joinStep();\n    applyQueuedRemoteCommands(dt);',
    '    joinStep();\n    seat2QueueCommand();   // split screen: the second local seat\'s command for this step (applied with the remote ones)\n' +
    '    if (seat2_ >= 0) seat2Ctl_.tickCameraCollision(dt);\n    applyQueuedRemoteCommands(dt);');
ed2('             l.settings.timeLimit, nb, l.bots.friendly, l.bots.enemy, botDifficultyName(l.bots.difficulty));\n',
    '             l.settings.timeLimit, nb, l.bots.friendly, l.bots.enemy, botDifficultyName(l.bots.difficulty));\n' +
    '    seat2_ = -1; seat2Pad_ = -1;   // a new match: no second local seat until a pad joins\n' +
    '    if (std::getenv("WFC_SEAT2BOT") && localPlayer_ >= 0)   // test only: seat 2 joins with the first player\'s pick\n' +
    '        addLocalSeat(1, match_.players()[(size_t)localPlayer_].selection, match_.players()[(size_t)localPlayer_].hasSelectedCharacter);\n');
fs.writeFileSync('src/game/World.cpp', crlf ? w.replace(/\n/g, '\r\n') : w);
console.log('ok');
