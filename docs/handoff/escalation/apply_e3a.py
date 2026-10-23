# One-off source edit (E3a): the horde's real bodies / weapons / health (RE MP_ESCALATION_RE §7.1 / §9.2; AssetTools escalation
# characters as "ESC_<id>" chassis) and the wave buffs (TnBuffSurvivalDecreaseDamageTaken L0-4: taken x0.9..0.5;
# TnBuffSurvivalIncreaseDamage L0-4: dealt x1.25..2.25; applied at spawn). Run from the Gameplay worktree root.
import sys
P = 'docs/handoff/escalation/'

def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if s.count(a) != 1: sys.exit(path + ': anchor not unique / missing: ' + a[:80])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Survival.h', [
    ("    struct Wave { int budget = 0, budgetAlive = 0, deficit = 0; };\n",
     "    struct Wave { int budget = 0, budgetAlive = 0, deficit = 0; int decreaseTaken = -1, increaseDealt = -1; };   // buff levels (-1 none)\n"),
    ("    struct Enemy { std::string type; int minWave = 0, maxWave = 0, minCount = 0, maxCount = 0, cost = 0, spawnType = 0; float power = 0.0f; };\n",
     "    struct Enemy { std::string type; int minWave = 0, maxWave = 0, minCount = 0, maxCount = 0, cost = 0, spawnType = 0; float power = 0.0f;\n"
     "                   std::string chassis; std::vector<std::string> robotWeapons, vehicleWeapons; float health = 150.0f; };   // E3a: the body\n"),
    ("    struct Slot { int player = -1; int enemy = -1; bool inUse = false; };",
     "    struct Slot { int player = -1; int enemy = -1; bool inUse = false; float takenMul = 1.0f, dealtMul = 1.0f; };"),
])

edit(P + 'WorldSurvival.cpp', [
    ("            out.waves.push_back({(int)w[i][\"budget\"].asFloat(0), (int)w[i][\"budgetAlive\"].asFloat(0), (int)w[i][\"deficit\"].asFloat(0)});\n",
     "        {\n"
     "            Wave wv{(int)w[i][\"budget\"].asFloat(0), (int)w[i][\"budgetAlive\"].asFloat(0), (int)w[i][\"deficit\"].asFloat(0)};\n"
     "            for (size_t b = 0; b < w[i][\"buffs\"].size(); ++b) {\n"
     "                const std::string cls = w[i][\"buffs\"][b][0].asString(); const int lv = w[i][\"buffs\"][b][1].asInt(0);\n"
     "                if (cls == \"TnBuffSurvivalDecreaseDamageTaken\") wv.decreaseTaken = lv;\n"
     "                else if (cls == \"TnBuffSurvivalIncreaseDamage\") wv.increaseDealt = lv;\n"
     "            }\n"
     "            out.waves.push_back(wv);\n"
     "        }\n"),
    ("            x.power = r[\"power\"].asFloat(0);\n",
     "            x.power = r[\"power\"].asFloat(0);\n"
     "            x.chassis = r[\"chassis\"].asString(); x.health = r[\"health\"].asFloat(150.0f);\n"
     "            for (size_t k = 0; k < r[\"robotWeapons\"].size(); ++k) x.robotWeapons.push_back(r[\"robotWeapons\"][k].asString());\n"
     "            for (size_t k = 0; k < r[\"vehicleWeapons\"].size(); ++k) x.vehicleWeapons.push_back(r[\"vehicleWeapons\"][k].asString());\n"),
    ("            match_.selectCharacter(p, id.selection);\n            preloadHeldWeaponModels(id.selection.weapons);\n            chassisAssets(resolveChassis(id.selection, match_.faction(p)));\n",
     "            // The entry's body (E3a): an iconic selection of its chassis; without one, the versus preset of the identity (PROV).\n"
     "            CharacterSelection sel = id.selection;\n"
     "            if (!svData_.enemies[e].chassis.empty()) { sel = CharacterSelection{}; sel.type = 1; sel.chassisId = svData_.enemies[e].chassis; }\n"
     "            match_.selectCharacter(p, sel);\n"
     "            preloadHeldWeaponModels(svData_.enemies[e].robotWeapons);\n"
     "            if (const ChassisAssets* ca = chassisAssets(resolveChassis(sel, match_.faction(p))); ca && !ca->error.empty())\n"
     "                LOG_WARN(\"escalation: horde body %s: %s\", svData_.enemies[e].chassis.c_str(), ca->error.c_str());\n"),
    ("            sv_.slots[(size_t)slot].inUse = true;\n",
     "            sv_.slots[(size_t)slot].inUse = true;\n"
     "            {   // the wave's buffs, applied at spawn: DecreaseDamageTaken L0-4 x0.9..0.5, IncreaseDamage L0-4 x1.25..2.25 [RE §2]\n"
     "                const SurvivalData::Wave& wv = svData_.waves[(size_t)sv_.currentWave];\n"
     "                sv_.slots[(size_t)slot].takenMul = wv.decreaseTaken >= 0 ? 0.9f - 0.1f * (float)wv.decreaseTaken : 1.0f;\n"
     "                sv_.slots[(size_t)slot].dealtMul = wv.increaseDealt >= 0 ? 1.25f + 0.25f * (float)wv.increaseDealt : 1.0f;\n"
     "            }\n"),
    ("float World::svZoneScore(int z, int t) const {",
     "// A horde pawn's loadout and health at its spawn (E3a): the entry's robot / vehicle weapons and its ROBODEF health.\n"
     "void World::svApplyHordeStats(Character& c, int player) {\n"
     "    for (const SurvivalState::Slot& s : sv_.slots) {\n"
     "        if (s.player != player) continue;\n"
     "        const SurvivalData::Enemy& E = svData_.enemies[(size_t)s.enemy];\n"
     "        std::vector<Weapon> robot;\n"
     "        for (const std::string& w : E.robotWeapons) if (const WeaponDef* d = findWeaponDef(w)) robot.push_back(Weapon::fromDef(*d));\n"
     "        if (!robot.empty() || !E.vehicleWeapons.empty()) c.setLoadout(robot, E.vehicleWeapons.empty() ? c.vehicleWeapons() : E.vehicleWeapons);\n"
     "        c.health().max = E.health; c.health().current = E.health;\n"
     "        return;\n"
     "    }\n"
     "}\n\n"
     "// The wave buffs on damage: a horde victim's DecreaseDamageTaken, a horde instigator's IncreaseDamage.\n"
     "float World::svDamageMul(int victim, int instigator) const {\n"
     "    float m = 1.0f;\n"
     "    for (const SurvivalState::Slot& s : sv_.slots) {\n"
     "        if (s.player == victim) m *= s.takenMul;\n"
     "        if (s.player == instigator && instigator != victim) m *= s.dealtMul;\n"
     "    }\n"
     "    return m;\n"
     "}\n\n"
     "float World::svZoneScore(int z, int t) const {"),
])

edit(P + 'Application_EscTest.cpp', [
    ("    check(D.weaponPickups.size() == 17, \"17 weapon pickup factories (hidden until bought)\");\n",
     "    check(D.weaponPickups.size() == 17, \"17 weapon pickup factories (hidden until bought)\");\n"
     "    check(D.enemies[0].chassis == \"ESC_Spider\" && D.enemies[0].health == 20.0f && D.waves[9].decreaseTaken == 0 && D.waves[13].increaseDealt == 0,\n"
     "          \"the horde table: Spider body ESC_Spider 20 HP; wave 10 DecreaseDamageTaken L0, wave 14 IncreaseDamage L0\");\n"),
    ("    run(3.0f);\n    bool spidersFirst",
     "    run(3.0f);\n"
     "    {   // the first spawn is a Spider in its own body with 20 HP\n"
     "        const game::Character* sp = nullptr;\n"
     "        for (const auto& s : S.slots) if (s.inUse && D.enemies[(size_t)s.enemy].type == \"Spider_AICD\") sp = world_.participantPawn(s.player);\n"
     "        check(sp && sp->chassis().id == \"ESC_Spider\" && sp->health().max == 20.0f, \"a Spider spawns in the ESC_Spider body with 20 HP\");\n"
     "    }\n"
     "    bool spidersFirst"),
])
print('ok')
