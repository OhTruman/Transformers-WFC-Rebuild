// MP transforming world actors (doors / bridges; see new/TransformingActors.h): World owns their state and collision, BotNav gets a hard
// block for the cells under colliding actors, WFC_TRANSACTTEST checks it. Identity: maps without these actors unchanged; BrokenHope /
// Remnant (closed doors now block) and Rust (its doors open at level start) change. Run from a tree root after copying
// new/TransformingActors.{h,cpp} to src/game/.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
if (!fs.existsSync('src/game/TransformingActors.cpp')) throw new Error('copy new/TransformingActors.{h,cpp} to src/game/ first');
ed('src/game/BotNav.h', [
    ['    bool usable(int cell, const Agent& a) const;\n',
     '    bool usable(int cell, const Agent& a) const;\n' +
     '    // A hard block (closed transforming doors): a blocked cell is not usable for anyone. Sized to the loaded mesh on first use.\n' +
     '    void setBlocked(int cell, bool b) {\n' +
     '        if (blocked_.size() != cells_.size()) blocked_.assign(cells_.size(), 0);\n' +
     '        if (cell >= 0 && (size_t)cell < blocked_.size()) blocked_[(size_t)cell] = b ? 1 : 0;\n' +
     '    }\n'],
    ['    std::vector<int> piece_;     // connected piece id per cell (portals + links, undirected)\n',
     '    std::vector<int> piece_;     // connected piece id per cell (portals + links, undirected)\n' +
     '    std::vector<uint8_t> blocked_;   // setBlocked (empty / another mesh\'s size: none)\n'],
]);
ed('src/game/BotNav.h', [
    ['    void setBlocked(int cell, bool b) {\n',
     '    // After setBlocked: the connected pieces again without the blocked cells (areas sealed off by closed doors leave the main\n' +
     '    // piece: roaming goals stay reachable and a search across a closed door fails at once instead of exhausting the budget).\n' +
     '    void recomputePieces() { computePieces(); }\n' +
     '    void setBlocked(int cell, bool b) {\n'],
    ['    std::vector<uint8_t> blocked_;   // setBlocked (empty / another mesh\'s size: none)\n',
     '    std::vector<uint8_t> blocked_;   // setBlocked (empty / another mesh\'s size: none)\n' +
     '    void computePieces();\n'],
]);
ed('src/game/BotNav.cpp', [
    ['bool BotNav::usable(int cell, const Agent& a) const {\n    if (cell < 0 || (size_t)cell >= cells_.size()) return false;\n',
     'bool BotNav::usable(int cell, const Agent& a) const {\n    if (cell < 0 || (size_t)cell >= cells_.size()) return false;\n' +
     '    if (blocked_.size() == cells_.size() && blocked_[(size_t)cell]) return false;   // a closed transforming door\n'],
    ['    // Connected pieces (portals + links, undirected).\n' +
     '    piece_.assign(cells_.size(), -1);\n' +
     '    std::vector<std::vector<int>> adj(cells_.size());\n' +
     '    for (size_t i = 0; i < cells_.size(); ++i) for (const Portal& p : cells_[i].portals) { adj[i].push_back(p.to); adj[(size_t)p.to].push_back((int)i); }\n' +
     '    for (const Link& l : links_) { adj[(size_t)l.from].push_back(l.to); adj[(size_t)l.to].push_back(l.from); }\n' +
     '    int pid = 0, best = 0;\n' +
     '    for (size_t i = 0; i < cells_.size(); ++i) {\n' +
     '        if (piece_[i] >= 0) continue;\n' +
     '        int n = 0; std::vector<int> st{(int)i}; piece_[i] = pid;\n' +
     '        while (!st.empty()) { int c = st.back(); st.pop_back(); ++n; for (int q : adj[(size_t)c]) if (piece_[(size_t)q] < 0) { piece_[(size_t)q] = pid; st.push_back(q); } }\n' +
     '        if (n > best) { best = n; mainPieceId_ = pid; }\n' +
     '        ++pid;\n' +
     '    }\n' +
     '    mainPiece_ = best;\n',
     '    computePieces();\n'],
    ['anchors_.size(), pid, best);\n',
     'anchors_.size(), [&] { int n = 0; for (int q : piece_) n = std::max(n, q + 1); return n; }(), mainPiece_);\n'],
    ['bool BotNav::usable(int cell, const Agent& a) const {\n',
     '// Connected pieces (portals + links, undirected); blocked cells belong to no piece and connect nothing.\n' +
     'void BotNav::computePieces() {\n' +
     '    const bool blk = blocked_.size() == cells_.size();\n' +
     '    auto open = [&](size_t c) { return !blk || !blocked_[c]; };\n' +
     '    piece_.assign(cells_.size(), -1);\n' +
     '    std::vector<std::vector<int>> adj(cells_.size());\n' +
     '    for (size_t i = 0; i < cells_.size(); ++i) for (const Portal& p : cells_[i].portals) { adj[i].push_back(p.to); adj[(size_t)p.to].push_back((int)i); }\n' +
     '    for (const Link& l : links_) { adj[(size_t)l.from].push_back(l.to); adj[(size_t)l.to].push_back(l.from); }\n' +
     '    int pid = 0, best = 0;\n' +
     '    for (size_t i = 0; i < cells_.size(); ++i) {\n' +
     '        if (piece_[i] >= 0 || !open(i)) continue;\n' +
     '        int n = 0; std::vector<int> st{(int)i}; piece_[i] = pid;\n' +
     '        while (!st.empty()) { int c = st.back(); st.pop_back(); ++n; for (int q : adj[(size_t)c]) if (piece_[(size_t)q] < 0 && open((size_t)q)) { piece_[(size_t)q] = pid; st.push_back(q); } }\n' +
     '        if (n > best) { best = n; mainPieceId_ = pid; }\n' +
     '        ++pid;\n' +
     '    }\n' +
     '    mainPiece_ = best;\n' +
     '}\n\n' +
     'bool BotNav::usable(int cell, const Agent& a) const {\n'],
]);
ed('src/game/World.h', [
    ['#include "game/MapState.h"\n', '#include "game/MapState.h"\n#include "game/TransformingActors.h"\n'],
    ['    MapState mapState_;\n',
     '    MapState mapState_;\n' +
     '    TransformingActors transforming_;   // the map\'s TnTransformingWorldActors (doors / bridges): state, collision, nav blocks\n'],
    ['    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);\n',
     '    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);\n' +
     '    // The map\'s transforming actors (Rendering: draw each actor\'s glb at matrix with its clip at t, unless hidden).\n' +
     '    const TransformingActors& transformingActors() const { return transforming_; }\n' +
     '    const CollisionWorld& pawnCollisionWorld() const { return collision_; }\n' +
     '    const CollisionWorld& weaponCollisionWorld() const { return weaponCollision_.valid() ? weaponCollision_ : collision_; }\n'],
]);
ed('src/game/World.cpp', [
    ['        mapState_.registerCollision(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr, pawnMovers, weaponMovers);\n',
     '        mapState_.registerCollision(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr, pawnMovers, weaponMovers);\n' +
     '        {   // TnTransformingWorldActors (AssetTools mp_transforming_actors.json; the frontend\'s manifest root)\n' +
     '            const char* mr = std::getenv("WFC_FRONTEND_MANIFESTS");\n' +
     '            transforming_.load(mapName_, mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests", collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n' +
     '        }\n'],
    ['    if (collision_.valid()) { TickTimer tt(13); mapState_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr); }\n',
     '    if (collision_.valid()) { TickTimer tt(13); mapState_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n' +
     '                              transforming_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr); }\n'],
    ['    if (collision_.valid()) mapState_.tick(0.0f, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n',
     '    if (collision_.valid()) mapState_.tick(0.0f, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n' +
     '    if (collision_.valid()) transforming_.resetForNewMatch(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);   // a fresh level\n'],
]);
ed('src/game/WorldBots.cpp', [[
    '    if (!botNav_.load(path)) LOG_WARN("bots: no nav data at %s (AssetTools bot_nav.py); bots will hold position and fight", path.c_str());\n',
    '    if (!botNav_.load(path)) LOG_WARN("bots: no nav data at %s (AssetTools bot_nav.py); bots will hold position and fight", path.c_str());\n' +
    '    if (botNav_.valid()) {   // closed transforming doors: the nav cells under them are unusable\n' +
    '        const int n = transforming_.applyNavBlocks(botNav_);\n' +
    '        if (n > 0) LOG_INFO("bots: %d nav cells blocked by transforming actors", n);\n' +
    '    }\n'
]]);
ed('src/core/Application.h', [[
    '    void runEventTest();\n', '    void runEventTest();\n    void runTransformingActorsTest();\n'
]]);
ed('src/core/Application_Frontend.cpp', [[
    '"WFC_EVENTTEST", ', '"WFC_EVENTTEST", "WFC_TRANSACTTEST", '
]]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_EVENTTEST")) { runEventTest(); return false; }',
     '    if (std::getenv("WFC_TRANSACTTEST")) { runTransformingActorsTest(); return false; }   // transforming doors / bridges (collision, nav)\n' +
     '    if (std::getenv("WFC_EVENTTEST")) { runEventTest(); return false; }'],
    ['// WFC_PACINGTEST: presented camera',
     '// WFC_TRANSACTTEST (WFC_MAP=MP_ESC_BrokenHope / MP_ESC_Remnant / MP_IAC_Rust): every transforming actor\'s collision. A closed door\n' +
     '// blocks a segment through each box (pawn and weapon worlds); an actor that opens at level start (Rust) no longer blocks its closed\n' +
     '// pose once open; retracted bridges block nothing; the nav cells under closed doors are blocked.\n' +
     'void Application::runTransformingActorsTest() {\n' +
     '    int checks = 0, fails = 0;\n' +
     '    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("TRANSACTTEST %s %s", ok ? "PASS" : "FAIL", what.c_str()); };\n' +
     '    const float dt = 1.0f / 60.0f;\n' +
     '    platform::InputFrame idle;\n' +
     '    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { world_.handleInput(idle, dt); world_.tick(dt); } };\n' +
     '    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);\n' +
     '    world_.launchMatch(L);\n' +
     '    const game::TransformingActors& T = world_.transformingActors();\n' +
     '    const bool hasActors = world_.mapName() == "MP_ESC_BrokenHope" || world_.mapName() == "MP_ESC_Remnant" || world_.mapName() == "MP_IAC_Rust";\n' +
     '    check(T.actors().empty() != hasActors, world_.mapName() + ": transforming actors loaded (" + std::to_string(T.actors().size()) + ")");\n' +
     '    // A segment along the box\'s thinnest axis through its centre, 1 m beyond each face.\n' +
     '    auto through = [&](const game::TransformingActors::Actor& a, const game::TransformingActors::Box& b, float t, core::Vec3& p0, core::Vec3& p1) {\n' +
     '        core::Vec3 lo, hi; T.boxBounds(a, b, t, lo, hi);\n' +
     '        const core::Vec3 c = (lo + hi) * 0.5f, e = hi - lo;\n' +
     '        core::Vec3 ax{1, 0, 0}; float h = e.x * 0.5f;\n' +
     '        if (e.z < e.x && e.z <= e.y) { ax = {0, 0, 1}; h = e.z * 0.5f; } else if (e.y < e.x && e.y < e.z) { ax = {0, 1, 0}; h = e.y * 0.5f; }\n' +
     '        p0 = c - ax * (h + 1.0f); p1 = c + ax * (h + 1.0f);\n' +
     '    };\n' +
     '    auto hitsActor = [&](const game::CollisionWorld& cw, const game::TransformingActors::Actor& a, bool weapon, const core::Vec3& p0, const core::Vec3& p1) {\n' +
     '        float tt; const int s = cw.dynamicSetHit(p0, p1, tt);\n' +
     '        for (const auto& bd : a.bodies) if (s >= 0 && s == (weapon ? bd.weaponSet : bd.pawnSet)) return true;\n' +
     '        return false;\n' +
     '    };\n' +
     '    // Versus on the Escalation-only maps (BrokenHope / Remnant): every actor Transformed from the start (no animation). Doors: open,\n' +
     '    // their closed pose clear; bridges: extended and solid (pawns and shots) at their pose.\n' +
     '    int doorOpen = 0, doorTotal = 0, bridgeSolid = 0, bridgeTotal = 0;\n' +
     '    for (const auto& a : T.actors()) {\n' +
     '        if (a.startDesired != 0) continue;\n' +
     '        if (a.bridge) {\n' +
     '            for (const auto& b : a.boxes) {\n' +
     '                core::Vec3 p0, p1; through(a, b, a.t, p0, p1); ++bridgeTotal;\n' +
     '                bridgeSolid += a.collide && !a.hidden && hitsActor(world_.pawnCollisionWorld(), a, false, p0, p1) && hitsActor(world_.weaponCollisionWorld(), a, true, p0, p1);\n' +
     '            }\n' +
     '        } else {\n' +
     '            ++doorTotal;\n' +
     '            bool clear = a.t >= a.length - 1e-3f;\n' +
     '            for (const auto& b : a.boxes) {\n' +
     '                // only the moving panels: a static body (Remnant\'s C_Body_XB side post) stays where it is in both poses\n' +
     '                core::Vec3 l0, h0, l1, h1; T.boxBounds(a, b, 0.0f, l0, h0); T.boxBounds(a, b, a.length, l1, h1);\n' +
     '                if (core::length(l1 - l0) + core::length(h1 - h0) < 0.5f) continue;\n' +
     '                core::Vec3 p0, p1; through(a, b, 0.0f, p0, p1); clear = clear && !hitsActor(world_.pawnCollisionWorld(), a, false, p0, p1);\n' +
     '            }\n' +
     '            doorOpen += clear;\n' +
     '        }\n' +
     '    }\n' +
     '    if (doorTotal) check(doorOpen == doorTotal, "versus: the Escalation-map doors start open, their doorways clear (" + std::to_string(doorOpen) + "/" + std::to_string(doorTotal) + ")");\n' +
     '    if (bridgeTotal) check(bridgeSolid == bridgeTotal, "versus: the bridges start extended and solid (" + std::to_string(bridgeSolid) + "/" + std::to_string(bridgeTotal) + " boxes)");\n' +
     '    // Level-start transforms (Rust): open after the clip; the closed pose no longer blocks.\n' +
     '    run(4.0f);\n' +
     '    int opened = 0, openTotal = 0;\n' +
     '    for (const auto& a : T.actors()) {\n' +
     '        if (a.startDesired != 1) continue;\n' +
     '        ++openTotal;\n' +
     '        bool anyBlock = false;\n' +
     '        for (const auto& b : a.boxes) { core::Vec3 p0, p1; through(a, b, 0.0f, p0, p1); anyBlock |= hitsActor(world_.pawnCollisionWorld(), a, false, p0, p1); }\n' +
     '        opened += a.t >= a.length - 1e-3f && !anyBlock;\n' +
     '    }\n' +
     '    if (openTotal) check(opened == openTotal, "level-start doors open and stop blocking their closed pose (" + std::to_string(opened) + "/" + std::to_string(openTotal) + ")");\n' +
     '    LOG_INFO("TRANSACTTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n' +
     '}\n\n' +
     '// WFC_PACINGTEST: presented camera'],
]);
console.log('ok');
