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
     '    void seat2QueueCommand();\n' +
     '    std::vector<PresentedFrame::DamageTaken> seat2Taken_;   // seat 2\'s damage-direction / hit-marker queues (as pendingDamage*)\n' +
     '    std::vector<PresentedFrame::DamageCaused> seat2Caused_;\n' +
     '    void fillSeats(PresentedFrame& p);\n' +
     '    void seat2NoteDamage(int victim, int instigator, float applied, bool killed);\n'],
    ['        bool extendedLobby = false;                     // World::extendedLobby() at this step\n',
     '        bool extendedLobby = false;                     // World::extendedLobby() at this step\n' +
     '        // Split screen (GAMEPLAY_SPLITSCREEN_PLAN.md step 2): the per-seat HUD state. seats[0] mirrors the local fields above;\n' +
     '        // seats[1] exists while a second local seat plays. The view camera is World::seatCamera (main thread, per render frame).\n' +
     '        // Each seat\'s death / respawn: dead here + the Respawn match events of its player.\n' +
     '        struct Seat {\n' +
     '            int player = -1;                            // the seat\'s match player\n' +
     '            bool dead = false;\n' +
     '            float health = 0.0f, healthMax = 0.0f; std::vector<float> segmentTops;\n' +
     '            int clip = 0, reserve = 0, mag = 0, reserveMax = 0;   // the held weapon (vehicle weapon in vehicle form)\n' +
     '            std::string weaponId, chassis;\n' +
     '            HudAimState aim;\n' +
     '            HudObservers hud2;                          // seat 2: abilities, grenade, weapon jam / spread, cloak / warcry buffs\n' +
     '            std::vector<Marker> markers;                // seat 2: seat 1\'s (same team), enemy labels by seat 2\'s line of sight\n' +
     '            std::vector<DamageTaken> damageTaken;       // since the last consumePresented (yaw relative to the seat\'s view)\n' +
     '            std::vector<DamageCaused> damageCaused;\n' +
     '        };\n' +
     '        std::vector<Seat> seats;\n'],
    ['                              presented_.damageTaken.clear(); presented_.damageCaused.clear(); presented_.steps = 0; }',
     '                              presented_.damageTaken.clear(); presented_.damageCaused.clear(); presented_.steps = 0;\n' +
     '                              for (auto& s : presented_.seats) { s.damageTaken.clear(); s.damageCaused.clear(); } }'],
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
    '    if (seat2_ >= 0 && seat2Ctl_.pawn()) seat2Ctl_.tickCameraCollision(dt);\n    applyQueuedRemoteCommands(dt);');
ed2('             l.settings.timeLimit, nb, l.bots.friendly, l.bots.enemy, botDifficultyName(l.bots.difficulty));\n',
    '             l.settings.timeLimit, nb, l.bots.friendly, l.bots.enemy, botDifficultyName(l.bots.difficulty));\n' +
    '    seat2_ = -1; seat2Pad_ = -1;   // a new match: no second local seat until a pad joins\n' +
    '    if (std::getenv("WFC_SEAT2BOT") && localPlayer_ >= 0)   // test only: seat 2 joins with the first player\'s pick\n' +
    '        addLocalSeat(1, match_.players()[(size_t)localPlayer_].selection, match_.players()[(size_t)localPlayer_].hasSelectedCharacter);\n');
ed2('    if (instigator == localPlayer_ && victim != localPlayer_ && applied > 0.0f) pendingDamageCaused_.push_back({victim, applied, h->isDead()});\n',
    '    if (instigator == localPlayer_ && victim != localPlayer_ && applied > 0.0f) pendingDamageCaused_.push_back({victim, applied, h->isDead()});\n' +
    '    seat2NoteDamage(victim, instigator, applied, h->isDead());   // split screen: seat 2\'s own queues\n');
ed2('    if (p.damageCaused.size() > 1024) p.damageCaused.erase(p.damageCaused.begin(), p.damageCaused.end() - 1024L);\n',
    '    if (p.damageCaused.size() > 1024) p.damageCaused.erase(p.damageCaused.begin(), p.damageCaused.end() - 1024L);\n' +
    '    fillSeats(p);   // split screen: the per-seat HUD state\n');
ed2('    b.steps += f.steps;\n    std::swap(f, b);\n',
    '    for (size_t i = 0; i < b.seats.size() && i < f.seats.size(); ++i) {   // split screen: each seat\'s unconsumed queues, front first\n' +
    '        if (b.seats[i].player != f.seats[i].player) continue;   // a seat that changed: the old player\'s entries go\n' +
    '        front(b.seats[i].damageTaken, f.seats[i].damageTaken); front(b.seats[i].damageCaused, f.seats[i].damageCaused);\n' +
    '    }\n' +
    '    b.steps += f.steps;\n    std::swap(f, b);\n' +
    '    for (auto& s : b.seats) { s.damageTaken.clear(); s.damageCaused.clear(); }\n');
ed2('\n    pendingDamageCaused_.clear();\n', '\n    pendingDamageCaused_.clear();\n    seat2Taken_.clear(); seat2Caused_.clear();\n');
fs.writeFileSync('src/game/World.cpp', crlf ? w.replace(/\n/g, '\r\n') : w);
console.log('ok');
