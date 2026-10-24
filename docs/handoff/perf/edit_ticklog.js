// WFC_TICKLOG (diagnostic, the 300 fps sim plan section 1): the step profiler gets 48 slots and a per-step accumulator; one CSV row per
// sim step (prefix / remainder / total ms, LOS / search / think / spawn-try counters, every slot); sub-slots for the Escalation
// director, the campaign runtime, hazards and the presented-frame markers; WFC_SOAK_GOD=1 (with WFC_QA=1) keeps the headless runner's
// human alive for soaks. TickProfile.h copied from argv[2]. Diagnostic only: no sim change. Run from the tree root.
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
fs.copyFileSync(path.join(G, 'docs/handoff/perf/TickProfile.h'), 'src/game/TickProfile.h');
const NAMES = '"sv.director", "sv.kismet", "sv.mech", "cp.vm", "cp.matinee", "cp.touches", "cp.destructibles", "cp.interacts", ' +
              '"cp.sentries+mines", "cp.factories", "hazards", "fill.markers", "prefix", "s43", "s44", "s45", "s46", "s47"';
ed('src/game/World.cpp', [
    ['    static bool on() { return env() || forced(); }' + NL + '    double acc[30] = {}; long n = 0;' + NL + '    const char* names[30] = {',
     '    static bool logOn() { static const bool o = std::getenv("WFC_TICKLOG") != nullptr; return o; }   // the per-step CSV' + NL +
     '    static bool on() { return env() || forced() || logOn(); }' + NL +
     '    double acc[48] = {}; double step[48] = {}; long n = 0;   // step: this sim step only (WFC_TICKLOG)' + NL +
     '    const char* names[48] = {'],
    ['"ab.sentry.search", "ab.sentry.pose", "presentedFill"};',
     '"ab.sentry.search", "ab.sentry.pose", "presentedFill",' + NL + '                             ' + NAMES + '};'],
    ['void tickProfAdd(int slot, double ms) { if (TickProf::on() && slot >= 0 && slot < 30) tickProf().acc[slot] += ms; }',
     'void tickProfAdd(int slot, double ms) { if (TickProf::on() && slot >= 0 && slot < 48) { tickProf().acc[slot] += ms; tickProf().step[slot] += ms; } }'],
    ['~TickTimer() { if (TickProf::on()) tickProf().acc[slot] += profNowMs() - t0; } };' + NL + '}' + NL,
     '~TickTimer() { if (TickProf::on()) { const double ms = profNowMs() - t0; tickProf().acc[slot] += ms; tickProf().step[slot] += ms; } } };' + NL +
     'std::atomic<long> gTickCounts[4];   // WFC_TICKLOG counters (LOS rays, path searches, thinks, spawn tries) since the last row' + NL + '}' + NL +
     'double tickProfNowMs() { return profNowMs(); }' + NL +
     'void tickCount(int which) { if (TickProf::on() && which >= 0 && which < 4) gTickCounts[which].fetch_add(1, std::memory_order_relaxed); }' + NL],
    ['    TickTimer tickTotal(14);   // WFC_TICKPROF: the whole step (this local part + the background part)' + NL,
     '    TickTimer tickTotal(14);   // WFC_TICKPROF: the whole step (this local part + the background part)' + NL +
     '    TickTimer prefixTotal(kTsPrefix);   // WFC_TICKLOG: this local part alone' + NL],
    ['    tickHazards(dt);' + NL, '    { TickTimer th(kTsHazards); tickHazards(dt); }' + NL],
    ['    p.markers.clear();' + NL, '    const double markersT0 = tickProfOn() ? profNowMs() : 0.0;   // WFC_TICKLOG fill.markers' + NL + '    p.markers.clear();' + NL],
    ['    prevMarkers_ = p.markers;' + NL, '    if (tickProfOn()) tickProfAdd(kTsFillMarkers, profNowMs() - markersT0);' + NL + '    prevMarkers_ = p.markers;' + NL],
    ['    remainderPending_ = false;' + NL + '    lastRemainderMs_ = profNowMs() - t0;' + NL,
     '    remainderPending_ = false;' + NL + '    lastRemainderMs_ = profNowMs() - t0;' + NL +
     '    if (TickProf::logOn()) {   // WFC_TICKLOG: this step\'s row (the prefix ran before this remainder)' + NL +
     '        static FILE* f = nullptr; static long stepNo = 0;' + NL +
     '        if (!f && (f = std::fopen(std::getenv("WFC_TICKLOG"), "w")) != nullptr) {' + NL +
     '            std::fprintf(f, "step,participants,horde,prefix_ms,remainder_ms,total_ms,los,searches,thinks,spawntries");' + NL +
     '            for (int i = 0; i < 48; ++i) std::fprintf(f, ",%s", tickProf().names[i]);' + NL +
     '            std::fprintf(f, "\\n");' + NL +
     '        }' + NL +
     '        if (f) {' + NL +
     '            int horde = 0; for (const MatchPlayer& mp : match_.players()) horde += mp.directorSpawned && mp.alive;' + NL +
     '            const double pre = tickProf().step[kTsPrefix];' + NL +
     '            std::fprintf(f, "%ld,%zu,%d,%.4f,%.4f,%.4f", ++stepNo, match_.players().size(), horde, pre, lastRemainderMs_, pre + lastRemainderMs_);' + NL +
     '            for (int c = 0; c < 4; ++c) std::fprintf(f, ",%ld", gTickCounts[c].exchange(0));' + NL +
     '            for (int i = 0; i < 48; ++i) std::fprintf(f, ",%.4f", tickProf().step[i]);' + NL +
     '            std::fprintf(f, "\\n");' + NL +
     '            if (stepNo % 600 == 0) std::fflush(f);' + NL +
     '        }' + NL +
     '        for (double& s : tickProf().step) s = 0.0;' + NL +
     '    }' + NL],
]);
{   // includes
    let s = fs.readFileSync('src/game/World.cpp', 'utf8');
    if (!s.includes('#include "game/TickProfile.h"')) {
        s = s.replace('#include "game/World.h"', '#include "game/World.h"' + (s.includes(CR + NL) ? CR + NL : NL) + '#include "game/TickProfile.h"' + (s.includes(CR + NL) ? CR + NL : NL) + '#include <atomic>');
        fs.writeFileSync('src/game/World.cpp', s);
    }
    for (const f of ['src/game/WorldSurvival.cpp', 'src/game/WorldCampaign.cpp', 'src/game/WorldBots.cpp']) {
        let t = fs.readFileSync(f, 'utf8');
        if (!t.includes('#include "game/TickProfile.h"')) {
            const nl = t.includes(CR + NL) ? CR + NL : NL;
            t = t.replace('#include "game/World.h"', '#include "game/World.h"' + nl + '#include "game/TickProfile.h"');
            fs.writeFileSync(f, t);
        }
    }
}
ed('src/game/WorldSurvival.cpp', [
    ['void World::tickSurvival(float dt) {' + NL, 'void World::tickSurvival(float dt) {' + NL + '    TickScope tsSurvival(kTsSurvival);   // WFC_TICKLOG' + NL],
    ['    svTickKismet();' + NL + '    svMechTick(dt);', '    { TickScope ts(kTsSvKismet); svTickKismet(); }' + NL + '    { TickScope ts(kTsSvMech); svMechTick(dt); }'],
    ['    for (int attempt = 0; attempt < 5; ++attempt) {' + NL, '    for (int attempt = 0; attempt < 5; ++attempt) {' + NL + '        tickCount(3);   // WFC_TICKLOG spawn tries' + NL],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    if (cp_.kismetBegun && !cp_.gameOver) cp_.vm.tick(dt);', '    { TickScope ts(kTsCpVm); if (cp_.kismetBegun && !cp_.gameOver) cp_.vm.tick(dt); }'],
    ['    if (!cp_.gameOver) cpTickFactories(dt);', '    { TickScope ts(kTsCpFactories); if (!cp_.gameOver) cpTickFactories(dt); }'],
    ['    cpTickMatinees(dt);', '    { TickScope ts(kTsCpMatinee); cpTickMatinees(dt); }'],
    ['    if (!cp_.gameOver) cpTickTouches();', '    { TickScope ts(kTsCpTouches); if (!cp_.gameOver) cpTickTouches(); }'],
    ['    cpTickDestructibles(dt);', '    { TickScope ts(kTsCpDestructibles); cpTickDestructibles(dt); }'],
    ['    cpTickInteracts(dt);', '    { TickScope ts(kTsCpInteracts); cpTickInteracts(dt); }'],
    ['    if (!cp_.gameOver) cpTickSentries(dt);' + NL + '    if (!cp_.gameOver) cpTickMines(dt);',
     '    { TickScope ts(kTsCpSentriesMines); if (!cp_.gameOver) cpTickSentries(dt); if (!cp_.gameOver) cpTickMines(dt); }'],
]);
ed('src/game/WorldBots.cpp', [
    ['bool World::botLineOfSight(const core::Vec3& from, const core::Vec3& to) const {' + NL,
     'bool World::botLineOfSight(const core::Vec3& from, const core::Vec3& to) const {' + NL + '    tickCount(0);   // WFC_TICKLOG LOS rays' + NL],
    ['        if (botNav_.findPath(pos, b.goal.pos, ag, path) && !path.empty())',
     '        tickCount(1);   // WFC_TICKLOG path searches' + NL + '        if (botNav_.findPath(pos, b.goal.pos, ag, path) && !path.empty())'],
    ['            botThink(*o, b);' + NL, '            tickCount(2);   // WFC_TICKLOG bot thinks' + NL + '            botThink(*o, b);' + NL],
]);
ed('src/core/Application.cpp', [
    ['        if (inProgress) started = true;' + NL,
     '        if (inProgress) started = true;' + NL +
     '        static bool soakGod = false;   // WFC_SOAK_GOD=1 (with WFC_QA=1): the human cannot die, so a perf soak runs its full time' + NL +
     '        if (inProgress && !soakGod && std::getenv("WFC_SOAK_GOD")) { soakGod = true; world_.qaSetGodMode(true); }' + NL +
     '        // WFC_SOAK_KILLEVERY=<s> (perf soaks): every s seconds the lowest live horde slot dies, so Escalation waves advance while the' + NL +
     '        // horde stays near its alive cap (the bot-driven human alone rarely clears a wave)' + NL +
     '        static const float soakKill = std::getenv("WFC_SOAK_KILLEVERY") ? (float)std::atof(std::getenv("WFC_SOAK_KILLEVERY")) : 0.0f;' + NL +
     '        if (soakKill > 0.0f && inProgress && steps > 0 && steps % std::max(1L, (long)std::lround(soakKill / step)) == 0)' + NL +
     '            for (const auto& sl : world_.survival().slots)' + NL +
     '                if (sl.inUse && sl.player >= 0 && world_.participantPawn(sl.player)) { world_.applyMatchDamage(sl.player, world_.localMatchPlayer(), 1.0e7f, false, "TnDamageType"); break; }' + NL],
]);
console.log('ok');
