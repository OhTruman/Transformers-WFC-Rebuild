# One-off source edit (E2b part 1): BrokenHope's Wall_Crashers map Kismet run as data by the director (crash walls, round gates, the
# terminal window), and ESCTEST checks for them plus the item generator's Taken path (G1). Run from the Gameplay worktree root.
import sys
P = 'docs/handoff/escalation/'

def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if s.count(a) != 1: sys.exit(path + ': anchor not unique / missing: ' + a[:80])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Survival.h', [
    ("    std::vector<Generator> generators;\n",
     "    std::vector<Generator> generators;\n"
     "    // Map Kismet as data (BrokenHope Wall_Crashers, RE E2b §4): trigger volumes by name; crash walls (any pawn's first touch: 200 on\n"
     "    // the destructible); round gates (from Round Begin count `round` a player touch turns the zone on, the untouch off); the window\n"
     "    // (a player touching `enable` arms an AI-only touch of `volume`: 200 on `wall`).\n"
     "    struct Shape {\n"
     "        core::Vec3 mn{0, 0, 0}, mx{0, 0, 0};\n"
     "        std::vector<std::array<float, 4>> planes;\n"
     "        bool contains(const core::Vec3& p) const {\n"
     "            if (p.x < mn.x || p.y < mn.y || p.z < mn.z || p.x > mx.x || p.y > mx.y || p.z > mx.z) return false;\n"
     "            for (const auto& q : planes) if (q[0] * p.x + q[1] * p.y + q[2] * p.z > q[3] + 1e-3f) return false;\n"
     "            return true;\n"
     "        }\n"
     "    };\n"
     "    std::vector<std::pair<std::string, Shape>> volumes;\n"
     "    struct CrashWall { int volume = -1; std::string wall; };\n"
     "    std::vector<CrashWall> crashWalls;\n"
     "    struct RoundGate { int volume = -1; int zone = -1; int round = 0; };\n"
     "    std::vector<RoundGate> roundGates;\n"
     "    struct Window { int enable = -1, volume = -1; std::string wall; float damage = 0.0f; };\n"
     "    Window window;\n"),
    ("    int roundBegins = 0, roundEnds = 0;\n",
     "    int roundBegins = 0, roundEnds = 0;\n"
     "    std::vector<uint8_t> crashDone, gateOn;               // per crash wall: fired; per round gate: its touch enabled\n"
     "    std::vector<std::vector<uint8_t>> gateInside;         // per round gate, per match player: inside last step\n"
     "    bool windowArmed = false, windowDone = false;\n"),
])

edit(P + 'WorldSurvival.cpp', [
    ("        for (int t = 0; t < kSvTypes; ++t)\n"
     "            for (size_t k = 0; k < j[\"initialSorted\"][(size_t)t].size(); ++k)",
     "        const assets::Json& km = j[\"kismet\"];\n"
     "        auto vol = [&](const std::string& name) {\n"
     "            for (size_t v = 0; v < out.volumes.size(); ++v) if (out.volumes[v].first == name) return (int)v;\n"
     "            const assets::Json& r = km[\"volumes\"][name];\n"
     "            if (!r.isObject()) return -1;\n"
     "            Shape s;\n"
     "            s.mn = {r[\"min\"][0].asFloat(0), r[\"min\"][1].asFloat(0), r[\"min\"][2].asFloat(0)};\n"
     "            s.mx = {r[\"max\"][0].asFloat(0), r[\"max\"][1].asFloat(0), r[\"max\"][2].asFloat(0)};\n"
     "            for (size_t k = 0; k < r[\"planes\"].size(); ++k) { const assets::Json& q = r[\"planes\"][k]; s.planes.push_back({q[0].asFloat(0), q[1].asFloat(0), q[2].asFloat(0), q[3].asFloat(0)}); }\n"
     "            out.volumes.push_back({name, s});\n"
     "            return (int)out.volumes.size() - 1;\n"
     "        };\n"
     "        for (size_t k = 0; k < km[\"crashWalls\"].size(); ++k) out.crashWalls.push_back({vol(km[\"crashWalls\"][k][0].asString()), km[\"crashWalls\"][k][1].asString()});\n"
     "        for (size_t k = 0; k < km[\"roundGates\"].size(); ++k) {\n"
     "            RoundGate g; g.volume = vol(km[\"roundGates\"][k][0].asString()); g.round = km[\"roundGates\"][k][2].asInt(0);\n"
     "            for (size_t z = 0; z < out.zones.size(); ++z) if (out.zones[z].id == km[\"roundGates\"][k][1].asString()) g.zone = (int)z;\n"
     "            out.roundGates.push_back(g);\n"
     "        }\n"
     "        if (km[\"window\"].isObject()) {\n"
     "            const assets::Json& w = km[\"window\"];\n"
     "            out.window = {vol(w[\"enable\"].asString()), vol(w[\"volume\"].asString()), w[\"wall\"].asString(), w[\"damage\"].asFloat(0)};\n"
     "        }\n"
     "        for (int t = 0; t < kSvTypes; ++t)\n"
     "            for (size_t k = 0; k < j[\"initialSorted\"][(size_t)t].size(); ++k)"),
    ("    sv_.factoryWasAvailable.assign(svData_.generators.size(), 1);\n",
     "    sv_.factoryWasAvailable.assign(svData_.generators.size(), 1);\n"
     "    sv_.crashDone.assign(svData_.crashWalls.size(), 0);\n"
     "    sv_.gateOn.assign(svData_.roundGates.size(), 0);\n"
     "    sv_.gateInside.assign(svData_.roundGates.size(), std::vector<uint8_t>(match_.players().size(), 0));\n"),
    ("    // Item generators (map Kismet per generator, PickupStatusChange Taken)",
     "    svTickKismet();\n"
     "    // Item generators (map Kismet per generator, PickupStatusChange Taken)"),
    ("float World::svZoneScore(int z, int t) const {",
     "// Wall_Crashers (RE E2b §4, C): see SurvivalData::crashWalls / roundGates / window.\n"
     "void World::svTickKismet() {\n"
     "    const std::vector<MatchPlayer>& P = match_.players();\n"
     "    auto damageWall = [&](const std::string& name, float amount) {\n"
     "        for (Destructible* d : destructibles_) if (d->name() == name) { d->applyDamage(*this, amount); return true; }\n"
     "        return false;\n"
     "    };\n"
     "    auto anyInside = [&](int v, int who) {   // who: 0 any pawn, 1 players only, 2 horde only\n"
     "        if (v < 0) return false;\n"
     "        for (size_t q = 0; q < P.size(); ++q) {\n"
     "            const bool bot = P[q].kind == ParticipantKind::Bot;\n"
     "            if ((who == 1 && bot) || (who == 2 && !P[q].directorSpawned)) continue;\n"
     "            const Character* c = participantPawn((int)q);\n"
     "            if (c && svData_.volumes[(size_t)v].second.contains(c->position())) return true;\n"
     "        }\n"
     "        return false;\n"
     "    };\n"
     "    for (size_t k = 0; k < svData_.crashWalls.size(); ++k)   // SeqEvent_Touch bPlayerOnly false, once: CauseDamage 200\n"
     "        if (!sv_.crashDone[k] && anyInside(svData_.crashWalls[k].volume, 0)) {\n"
     "            sv_.crashDone[k] = 1;\n"
     "            damageWall(svData_.crashWalls[k].wall, 200.0f);\n"
     "            LOG_INFO(\"escalation: crash wall %s broken\", svData_.crashWalls[k].wall.c_str());\n"
     "        }\n"
     "    for (size_t k = 0; k < svData_.roundGates.size(); ++k) {   // SeqCond_Increment A == B at Round Begin -> the touch turns on\n"
     "        const SurvivalData::RoundGate& g = svData_.roundGates[k];\n"
     "        if (!sv_.gateOn[k] && sv_.roundBegins >= g.round) sv_.gateOn[k] = 1;\n"
     "        if (sv_.gateInside[k].size() < P.size()) sv_.gateInside[k].resize(P.size(), 0);\n"
     "        for (size_t q = 0; q < P.size(); ++q) {   // per pawn events: any Touched -> on, any UnTouched -> off\n"
     "            const Character* c = P[q].kind != ParticipantKind::Bot ? participantPawn((int)q) : nullptr;\n"
     "            const uint8_t in = c && g.volume >= 0 && svData_.volumes[(size_t)g.volume].second.contains(c->position()) ? 1 : 0;\n"
     "            if (sv_.gateOn[k] && in != sv_.gateInside[k][q]) svSetZoneActive(g.zone, in != 0);\n"
     "            sv_.gateInside[k][q] = in;\n"
     "        }\n"
     "    }\n"
     "    const SurvivalData::Window& w = svData_.window;\n"
     "    if (w.volume >= 0 && !sv_.windowDone) {\n"
     "        if (!sv_.windowArmed && anyInside(w.enable, 1)) sv_.windowArmed = true;   // a player arms the AI-only touch\n"
     "        if (sv_.windowArmed && anyInside(w.volume, 2)) { sv_.windowDone = true; damageWall(w.wall, w.damage); LOG_INFO(\"escalation: window %s smashed by the horde\", w.wall.c_str()); }\n"
     "    }\n"
     "}\n\n"
     "float World::svZoneScore(int z, int t) const {"),
])
s = open(P + 'WorldSurvival.cpp', encoding='utf-8').read()
if '#include "game/Destructible.h"' not in s:
    s = s.replace('#include "game/World.h"\n', '#include "game/World.h"\n#include "game/Destructible.h"\n', 1)
    open(P + 'WorldSurvival.cpp', 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Application_EscTest.cpp', [
    ("        LOG_INFO(\"ESCTEST ammo bought in %.2f s, power %.0f -> %.0f\", t, p0, S.power[(size_t)me]);\n    }",
     "        LOG_INFO(\"ESCTEST ammo bought in %.2f s, power %.0f -> %.0f\", t, p0, S.power[(size_t)me]);\n"
     "        // G1: the Taken chain. Take the crate (reserve emptied): the level drop re-applies UnHide + the volume off, so it stays out;\n"
     "        // the volume reopens 2 s later. A second take at level 0 only hides it.\n"
     "        const game::PickupFactory* crate = nullptr;\n"
     "        for (const game::PickupFactory* f : world_.pickupFactories()) if (f->name() == \"TnAmmoCratePickupFactory_13346\") crate = f;\n"
     "        auto take = [&] {\n"
     "            static core::Vec3 at; at = crate->position();\n"
     "            world_.player().pawn().weapon().reserve = 0;\n"
     "            pin = &at; for (int k = 0; k < 20 && crate->available(); ++k) step(); pin = nullptr;\n"
     "        };\n"
     "        if (crate && gi >= 0) {\n"
     "            take();\n"
     "            check(!crate->hidden() && S.pool[(size_t)gi] == 0.0f && !S.generatorDone[(size_t)gi] && S.generatorOff[(size_t)gi],\n"
     "                  \"G1: the first take drops the level and re-fires New Power Level: the crate stays out, the volume stays off\");\n"
     "            run(2.2f);\n"
     "            check(!S.generatorOff[(size_t)gi] && crate->available(), \"G1: 2 s later the volume is back; the crate respawned (2 s)\");\n"
     "            take();\n"
     "            check(crate->hidden(), \"G1: a take at level 0 only hides it (until the generator is bought again)\");\n"
     "        }\n"
     "    }"),
    ("    run(25.5f);\n    // Loss: every player dead at once.",
     "    {   // Wall_Crashers: a crash wall breaks on a pawn's first touch\n"
     "        const game::SurvivalData::CrashWall& cw = D.crashWalls[0];\n"
     "        const game::SurvivalData::Shape& v = D.volumes[(size_t)cw.volume].second;\n"
     "        static core::Vec3 at; at = (v.mn + v.mx) * 0.5f;\n"
     "        pin = &at; step(); step(); pin = nullptr;\n"
     "        float hp = 1.0f; for (const game::Destructible* d : world_.destructibles()) if (d->name() == cw.wall) hp = d->health();\n"
     "        check(D.crashWalls.size() == 4 && S.crashDone[0] && hp <= 0.0f, \"Wall_Crashers: a pawn's first touch breaks the crash wall (200 damage)\");\n"
     "    }\n"
     "    run(25.5f);\n    // Loss: every player dead at once."),
])
s = open(P + 'Application_EscTest.cpp', encoding='utf-8').read()
if '#include "game/Destructible.h"' not in s:
    s = s.replace('#include "game/PickupFactory.h"\n', '#include "game/PickupFactory.h"\n#include "game/Destructible.h"\n', 1)
    open(P + 'Application_EscTest.cpp', 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
