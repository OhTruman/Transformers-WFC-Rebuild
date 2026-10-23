# One-off source edit (E2b part 2 glue): gen_escalation.py weapon pickups, edit_escalation_e2b.js (World.h declarations, Character
# inventory accessors, the SV spawn loadout hook, the director owning carried-heavy tosses in survival), ESCTEST weapon checks.
# Run from the Gameplay worktree root.
import sys

def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if s.count(a) != 1: sys.exit(path + ': anchor not unique / missing: ' + a[:80])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

edit('tools/gameplay/gen_escalation.py', [
    ("    out['kismet'] = dict(k, volumes=tvs)\n",
     "    out['kismet'] = dict(k, volumes=tvs)\n"
     "    # TnWeaponPickupFactory (AssetTools weapon_pickups): our weapon id = the inventory class without TransContent.TnWeapon.\n"
     "    out['weaponPickups'] = [{'actor': w['actor'], 'weapon': w['inventory_type'].split('.')[-1].replace('TnWeapon', ''),\n"
     "                             'respawn': w.get('respawn_time') or 2.0, 'pos': [round(v, 3) for v in w['location_gltf']],\n"
     "                             'yaw': round(math.atan2(-math.cos(math.radians(w.get('yaw_deg', 0.0))), -math.sin(math.radians(w.get('yaw_deg', 0.0)))), 5)}\n"
     "                            for w in geo.get('weapon_pickups', [])]\n"),
])

JS_OLD = ("ed('src/game/World.h', [\n"
          "    ['    void svFireGenerator(int generator, bool levelDrop = false);\\n',\n"
          "     '    void svFireGenerator(int generator, bool levelDrop = false);\\n    void svTickKismet();   // Wall_Crashers (map Kismet as data)\\n'],\n"
          "]);\n")
JS_NEW = ("ed('src/game/World.h', [\n"
          "    ['    void svFireGenerator(int generator, bool levelDrop = false);\\n',\n"
          "     '    void svFireGenerator(int generator, bool levelDrop = false);\\n    void svTickKismet();   // Wall_Crashers (map Kismet as data)\\n' +\n"
          "     '    // Escalation weapons (E2b part 2): the SV spawn loadout, the pickup / dropped weapon take rules, the factories and drops.\\n' +\n"
          "     '    Character* svMutablePawn(int player);\\n    void svApplySpawnLoadout(Character& c);\\n' +\n"
          "     '    bool svTakeWeapon(Character& c, int player, const Weapon& got);\\n    void svTickWeaponPickups(float dt);\\n'],\n"
          "]);\n"
          "ed('src/game/Character.h', [\n"
          "    ['    Weapon& weapon() { return inventory_[(size_t)activeWeapon_]; }\\n',\n"
          "     '    Weapon& weapon() { return inventory_[(size_t)activeWeapon_]; }\\n' +\n"
          "     '    // Escalation pickups: the robot inventory by index (a temp WT_Heavy weapon, when held, is the last); addWeapon keeps it last.\\n' +\n"
          "     '    int weaponCount() const { return (int)inventory_.size(); }\\n' +\n"
          "     '    Weapon& weaponAt(int i) { return inventory_[(size_t)i]; }\\n' +\n"
          "     '    const Weapon& weaponAt(int i) const { return inventory_[(size_t)i]; }\\n' +\n"
          "     '    void addWeapon(const Weapon& w) { if (tempWeapon_ && !inventory_.empty()) inventory_.insert(inventory_.end() - 1, w); else inventory_.push_back(w); }\\n'],\n"
          "]);\n"
          "ed('src/game/World.cpp', [\n"
          "    ['                        loadoutRefused_ = applyLoadout(&mp.selection);\\n',\n"
          "     '                        loadoutRefused_ = applyLoadout(&mp.selection);\\n' +\n"
          "     '                        if (sv_.phase != SurvivalState::Phase::Off) svApplySpawnLoadout(pc);   // SetPlayerDefaults (SV)\\n'],\n"
          "    ['                        if (chassisOk) applyCharacterTo(o->pawn(), &op.selection, &op);\\n',\n"
          "     '                        if (chassisOk) applyCharacterTo(o->pawn(), &op.selection, &op);\\n' +\n"
          "     '                        if (chassisOk && sv_.phase != SurvivalState::Phase::Off && op.kind != ParticipantKind::Bot) svApplySpawnLoadout(o->pawn());\\n'],\n"
          "    ['    if (pc.tempWeapon_ == 2 && (pc.isTransforming()',\n"
          "     '    if (!match_.settings().survival && pc.tempWeapon_ == 2 && (pc.isTransforming()'],   // Escalation: the director tosses carried heavies\n"
          "]);\n")
edit('docs/handoff/escalation/edit_escalation_e2b.js', [(JS_OLD, JS_NEW)])

edit('docs/handoff/escalation/Application_EscTest.cpp', [
    ("    check(m.players()[(size_t)me].team == 0, \"the player is on BrokenHope's co-op team (Autobots)\");\n",
     "    check(m.players()[(size_t)me].team == 0, \"the player is on BrokenHope's co-op team (Autobots)\");\n"
     "    check(D.weaponPickups.size() == 17, \"17 weapon pickup factories (hidden until bought)\");\n"),
    ("    check(m.state() == game::Match::State::InProgress && !world_.localPlayerDead() && ally->spawned() && hordeAlive == 0,\n",
     "    {   // SetPlayerDefaults (SV): the AssaultRifle replaces the primary and secondary\n"
     "        const auto& pc = world_.player().pawn();\n"
     "        int robot = 0; bool ar = false;\n"
     "        for (int i = 0; i < pc.weaponCount(); ++i)\n"
     "            if (pc.weaponAt(i).fireType != game::WeaponFire::Grenade) { ++robot; ar = ar || (pc.weaponAt(i).def && std::string(pc.weaponAt(i).def->id) == \"AssaultRifle\"); }\n"
     "        check(robot == 1 && ar, \"the SV spawn loadout: the AssaultRifle only (plus the grenade bag)\");\n"
     "    }\n"
     "    check(m.state() == game::Match::State::InProgress && !world_.localPlayerDead() && ally->spawned() && hordeAlive == 0,\n"),
    ("        LOG_INFO(\"ESCTEST door bought in %.2f s, power %.0f, %d purchases\", t, S.power[(size_t)me], S.purchases);\n    }",
     "        LOG_INFO(\"ESCTEST door bought in %.2f s, power %.0f, %d purchases\", t, S.power[(size_t)me], S.purchases);\n    }\n"
     "    {   // a weapon generator: FlakGrenade 1835 (75) unhides its factory; the press takes the bag (a different bag is tossed)\n"
     "        const float t = buy(\"1835\", 6.0f);\n"
     "        int k = -1; for (size_t i = 0; i < D.weaponPickups.size(); ++i) if (D.weaponPickups[i].weapon == \"FlakGrenades\") k = (int)i;\n"
     "        check(t > 0.0f && k >= 0 && !S.weaponPicks[(size_t)k].hidden, \"FlakGrenade generator: its weapon factory unhidden\");\n"
     "        if (k >= 0) {\n"
     "            static core::Vec3 at; at = D.weaponPickups[(size_t)k].pos;\n"
     "            const auto& pc = world_.player().pawn();\n"
     "            bool hadOther = false;\n"
     "            for (int i = 0; i < pc.weaponCount(); ++i)\n"
     "                if (pc.weaponAt(i).fireType == game::WeaponFire::Grenade && pc.weaponAt(i).def && std::string(pc.weaponAt(i).def->id) != \"FlakGrenades\" &&\n"
     "                    pc.weaponAt(i).ammo + pc.weaponAt(i).reserve > 0) hadOther = true;\n"
     "            pin = &at; step(); step(); interact = true; step(); interact = false; step(); pin = nullptr;\n"
     "            bool flak = false;\n"
     "            for (int i = 0; i < pc.weaponCount(); ++i) flak = flak || (pc.weaponAt(i).def && std::string(pc.weaponAt(i).def->id) == \"FlakGrenades\");\n"
     "            check(flak && !S.weaponPicks[(size_t)k].available && (!hadOther || S.drops.size() == 1),\n"
     "                  \"the press takes the Flak grenade bag; a different bag is tossed as a dropped pickup\");\n"
     "            LOG_INFO(\"ESCTEST flak taken: had another bag %d, drops %zu, takes %d\", (int)hadOther, S.drops.size(), S.weaponTakes);\n"
     "        }\n"
     "    }"),
])
print('ok')
