# One-off source edit (E2b part 2): Escalation weapon pickups (RE ESCALATION_E2B_ANSWERS_RE.md 45a2a08; AssetTools weapon_pickups):
# TnWeaponPickupFactory (hidden until its generator UnHides it, RespawnTime 2 s), TnDroppedPickupWeapon (a tossed weapon with its
# clip / reserve), the take rules (an owned weapon by touch as ammo unless the reserve is full; an unowned one by the Interact press,
# the closest touching pickup), DropRobotWeapon's slot rules, grenade bags (a different bag tossed; the same merges), carried heavies
# (the temp WT_Heavy slot; tossed on a swap / transform / death), and SetPlayerDefaults (primary + secondary removed, the AssaultRifle
# given). PROV until AssetTools' PlayerData values land: pickup ammo = the weapon's clip + initial reserve (MultiplayerData rows);
# dropped weapons live 30 s. Run from the Gameplay worktree root.
import sys
P = 'docs/handoff/escalation/'

def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if s.count(a) != 1: sys.exit(path + ': anchor not unique / missing: ' + a[:80])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Survival.h', [
    ('#include "core/Math.h"\n', '#include "core/Math.h"\n#include "game/Weapon.h"\n'),
    ("    Window window;\n",
     "    Window window;\n"
     "    // TnWeaponPickupFactory (AssetTools weapon_pickups): the weapon id (our WeaponDef id), RespawnTime, location / yaw.\n"
     "    struct WeaponPickup { std::string actor, weapon; float respawn = 2.0f; core::Vec3 pos{0, 0, 0}; float yaw = 0.0f; };\n"
     "    std::vector<WeaponPickup> weaponPickups;\n"),
    ("    bool windowArmed = false, windowDone = false;\n",
     "    bool windowArmed = false, windowDone = false;\n"
     "    // Weapon pickups: per factory hidden (until bought) / available / respawn timer; dropped weapons (TnDroppedPickupWeapon) with the\n"
     "    // tossed weapon's state; per match player the Interact press edge for the contextual pickup.\n"
     "    struct WeaponPick { bool hidden = true, available = true; float respawn = 0.0f; };\n"
     "    std::vector<WeaponPick> weaponPicks;\n"
     "    struct Drop { Weapon w; core::Vec3 pos{0, 0, 0}; float life = 0.0f; };\n"
     "    std::vector<Drop> drops;\n"
     "    std::vector<uint8_t> pickupPress;\n"
     "    int weaponTakes = 0, ammoTakes = 0, tosses = 0;\n"),
])

edit(P + 'WorldSurvival.cpp', [
    # --- load
    ("        for (int t = 0; t < kSvTypes; ++t)\n"
     "            for (size_t k = 0; k < j[\"initialSorted\"][(size_t)t].size(); ++k)",
     "        const assets::Json& wp = j[\"weaponPickups\"];\n"
     "        for (size_t k = 0; k < wp.size(); ++k) {\n"
     "            const assets::Json& r = wp[k];\n"
     "            WeaponPickup w; w.actor = r[\"actor\"].asString(); w.weapon = r[\"weapon\"].asString(); w.respawn = r[\"respawn\"].asFloat(2.0f);\n"
     "            w.pos = {r[\"pos\"][0].asFloat(0), r[\"pos\"][1].asFloat(0), r[\"pos\"][2].asFloat(0)}; w.yaw = r[\"yaw\"].asFloat(0);\n"
     "            out.weaponPickups.push_back(w);\n"
     "        }\n"
     "        for (int t = 0; t < kSvTypes; ++t)\n"
     "            for (size_t k = 0; k < j[\"initialSorted\"][(size_t)t].size(); ++k)"),
    # --- setup
    ("    sv_.crashDone.assign(svData_.crashWalls.size(), 0);\n",
     "    sv_.crashDone.assign(svData_.crashWalls.size(), 0);\n"
     "    sv_.weaponPicks.assign(svData_.weaponPickups.size(), SurvivalState::WeaponPick{});\n"
     "    sv_.pickupPress.assign(match_.players().size(), 0);\n"),
    # --- generator action: unhide a weapon factory
    ("        } else if (a.cls == \"TnTransformingWorldActor\") {\n",
     "        } else if (a.cls == \"TnWeaponPickupFactory\" || a.cls == \"TnPickupFactory\") {\n"
     "            for (size_t k = 0; k < svData_.weaponPickups.size(); ++k)\n"
     "                if (svData_.weaponPickups[k].actor == a.target) { sv_.weaponPicks[k].hidden = a.op == \"hide\"; ok = true; }\n"
     "        } else if (a.cls == \"TnTransformingWorldActor\") {\n"),
    # --- the press edge from the generator loop
    ("        const bool press = held && !sv_.prevInteract[q];\n",
     "        const bool press = held && !sv_.prevInteract[q];\n"
     "        if (sv_.pickupPress.size() <= q) sv_.pickupPress.resize(q + 1, 0);\n"
     "        sv_.pickupPress[q] = press ? 1 : 0;\n"),
    # --- tick the pickups after the generators
    ("    svTickKismet();\n",
     "    svTickKismet();\n"
     "    svTickWeaponPickups(dt);\n"),
    # --- the functions
    ("float World::svZoneScore(int z, int t) const {",
     "Character* World::svMutablePawn(int p) {\n"
     "    if (p == localPlayer_) return localDead_ ? nullptr : &player_.pawn();\n"
     "    if ((size_t)p < oppByPlayer_.size()) { MatchOpponent* o = oppByPlayer_[(size_t)p]; return o && o->spawned() ? &o->pawn() : nullptr; }\n"
     "    return nullptr;\n"
     "}\n\n"
     "// SetPlayerDefaults (TnPlayerPawnSurvival) [RE §1]: the primary and secondary weapons removed, a TnWeaponAssaultRifle given; the\n"
     "// grenade bag stays (PROV: RE asked). Every spawn.\n"
     "void World::svApplySpawnLoadout(Character& c) {\n"
     "    const WeaponDef* ar = findWeaponDef(\"AssaultRifle\");\n"
     "    if (!ar) return;\n"
     "    std::vector<Weapon> robot{Weapon::fromDef(*ar)};\n"
     "    for (int i = 0; i < c.weaponCount(); ++i) if (c.weaponAt(i).fireType == WeaponFire::Grenade) robot.push_back(c.weaponAt(i));\n"
     "    c.setLoadout(robot, c.vehicleWeapons());\n"
     "}\n\n"
     "// A pickup's weapon: PostInstantiateFromPickupFactory -> SetupAmmoForDefaultPlayerPickup [RE E2b §1]. PROV: a full clip + the initial\n"
     "// reserve until the PlayerData DefaultPickupAmount values land.\n"
     "static Weapon svPickupWeapon(const WeaponDef& d) {\n"
     "    Weapon w = Weapon::fromDef(d);\n"
     "    w.ammo = w.magSize; w.reserve = std::min(w.reserveMax, d.initialReserve);\n"
     "    return w;\n"
     "}\n\n"
     "// Taking a weapon pickup (factory or dropped) [RE E2b §1-§3]: returns whether it was taken.\n"
     "bool World::svTakeWeapon(Character& c, int player, const Weapon& got) {\n"
     "    if (!got.def) return false;\n"
     "    const std::string id = got.def->id;\n"
     "    const int type = got.def->typeCode;   // 0 robot weapon, 1 heavy, 4 grenade bag\n"
     "    for (int i = 0; i < c.weaponCount(); ++i) {   // owned: AddInventory merges -> AddAmmo(clip + reserve), reload if empty\n"
     "        Weapon& w = c.weaponAt(i);\n"
     "        if (!w.def || id != w.def->id) continue;\n"
     "        if (w.reserve >= w.reserveMax) return false;   // DenyPickupQuery: the reserve is full\n"
     "        w.reserve = std::min(w.reserveMax, w.reserve + got.ammo + got.reserve);\n"
     "        if (w.ammo == 0) { const int take = std::min(w.magSize, w.reserve); w.ammo = take; w.reserve -= take; }\n"
     "        ++sv_.ammoTakes;\n"
     "        return true;\n"
     "    }\n"
     "    auto toss = [&](const Weapon& w) {   // TnDroppedPickupWeapon with the tossed weapon's clip / reserve\n"
     "        if (!w.def) return;\n"
     "        SurvivalState::Drop d; d.w = w; d.pos = c.position(); d.life = 30.0f;   // PROV: LifeSpan (RE asked)\n"
     "        sv_.drops.push_back(d); ++sv_.tosses;\n"
     "    };\n"
     "    if (type == 4) {   // a grenade bag: holding a different bag -> toss it, take the new one\n"
     "        for (int i = 0; i < c.weaponCount(); ++i)\n"
     "            if (c.weaponAt(i).fireType == WeaponFire::Grenade) { toss(c.weaponAt(i)); c.weaponAt(i) = got; ++sv_.weaponTakes; return true; }\n"
     "        c.addWeapon(got); ++sv_.weaponTakes; return true;\n"
     "    }\n"
     "    // DropRobotWeapon: (2) the current heavy goes; (3) a free primary / secondary slot or a heavy taken -> nothing more;\n"
     "    // (4) else the current robot weapon, else the primary, else the secondary.\n"
     "    if (c.tempWeapon_ == 2) { toss(c.weapon()); c.removeTempWeapon(); }\n"
     "    if (type == 1) { c.grantTempWeapon(*got.def, 2, 1.0e9f); c.weapon() = got; ++sv_.weaponTakes; return true; }\n"
     "    std::vector<int> robot;\n"
     "    for (int i = 0; i < c.weaponCount(); ++i) if (c.weaponAt(i).fireType != WeaponFire::Grenade && c.weaponAt(i).def) robot.push_back(i);\n"
     "    if (robot.size() < 2) { c.addWeapon(got); ++sv_.weaponTakes; return true; }\n"
     "    int slot = robot[0];\n"
     "    for (int i : robot) if (i == c.activeWeaponIndex()) slot = i;\n"
     "    toss(c.weaponAt(slot));\n"
     "    c.weaponAt(slot) = got;\n"
     "    ++sv_.weaponTakes;\n"
     "    (void)player;\n"
     "    return true;\n"
     "}\n\n"
     "void World::svTickWeaponPickups(float dt) {\n"
     "    const std::vector<MatchPlayer>& P = match_.players();\n"
     "    for (SurvivalState::WeaponPick& w : sv_.weaponPicks)   // RespawnTime 2 s\n"
     "        if (!w.available && (w.respawn -= dt) <= 0.0f) w.available = true;\n"
     "    for (size_t k = 0; k < sv_.drops.size();)\n"
     "        if ((sv_.drops[k].life -= dt) <= 0.0f) sv_.drops.erase(sv_.drops.begin() + (long)k); else ++k;\n"
     "    auto near = [](const core::Vec3& a, const core::Vec3& b) {   // the pickup cylinder (200 UU radius, 100 UU half height) vs the pawn\n"
     "        const core::Vec3 d = a - b; return std::sqrt(d.x * d.x + d.z * d.z) < 2.0f && std::fabs(d.y) < 2.5f;\n"
     "    };\n"
     "    for (size_t q = 0; q < P.size(); ++q) {\n"
     "        if (P[q].kind == ParticipantKind::Bot) continue;   // AiInventoryType None: the horde never takes one\n"
     "        Character* c = svMutablePawn((int)q);\n"
     "        if (!c || c->moveForm() != Form::Robot || c->isTransforming() || c->health().isDead()) continue;   // CanPickupInventory\n"
     "        // Carried heavies are tossed on a swap / transform (WT_Heavy).\n"
     "        if (c->tempWeapon_ == 2 && c->tempDropRequested_) {\n"
     "            SurvivalState::Drop d; d.w = c->weapon(); d.pos = c->position(); d.life = 30.0f; sv_.drops.push_back(d); ++sv_.tosses;\n"
     "            c->removeTempWeapon(); c->tempDropRequested_ = false;\n"
     "        }\n"
     "        const bool press = q < sv_.pickupPress.size() && sv_.pickupPress[q];\n"
     "        int bestF = -1, bestD = -1; float bestDist = 1e9f;\n"
     "        for (size_t k = 0; k < svData_.weaponPickups.size(); ++k) {\n"
     "            SurvivalState::WeaponPick& w = sv_.weaponPicks[k];\n"
     "            if (w.hidden || !w.available || !near(svData_.weaponPickups[k].pos, c->position())) continue;\n"
     "            const WeaponDef* def = findWeaponDef(svData_.weaponPickups[k].weapon.c_str());\n"
     "            if (!def) continue;\n"
     "            bool owned = false;\n"
     "            for (int i = 0; i < c->weaponCount(); ++i) owned = owned || (c->weaponAt(i).def && std::string(c->weaponAt(i).def->id) == def->id);\n"
     "            if (owned) {   // PickupWhenTouched: an owned weapon is ammo\n"
     "                if (svTakeWeapon(*c, (int)q, svPickupWeapon(*def))) { w.available = false; w.respawn = svData_.weaponPickups[k].respawn; }\n"
     "                continue;\n"
     "            }\n"
     "            const float d = core::length(svData_.weaponPickups[k].pos - c->position());\n"
     "            if (d < bestDist) { bestDist = d; bestF = (int)k; bestD = -1; }\n"
     "        }\n"
     "        for (size_t k = 0; k < sv_.drops.size(); ++k) {\n"
     "            const SurvivalState::Drop& d = sv_.drops[k];\n"
     "            if (!near(d.pos, c->position())) continue;\n"
     "            bool owned = false;\n"
     "            for (int i = 0; i < c->weaponCount(); ++i) owned = owned || (c->weaponAt(i).def && d.w.def && std::string(c->weaponAt(i).def->id) == d.w.def->id);\n"
     "            if (owned) { if (svTakeWeapon(*c, (int)q, d.w)) { sv_.drops.erase(sv_.drops.begin() + (long)k); } break; }\n"
     "            const float dist = core::length(d.pos - c->position());\n"
     "            if (dist < bestDist) { bestDist = dist; bestD = (int)k; bestF = -1; }\n"
     "        }\n"
     "        if (!press) continue;   // TnPickupManager: the press takes the closest valid touching pickup\n"
     "        if (bestF >= 0) {\n"
     "            const WeaponDef* def = findWeaponDef(svData_.weaponPickups[(size_t)bestF].weapon.c_str());\n"
     "            if (def && svTakeWeapon(*c, (int)q, svPickupWeapon(*def))) { sv_.weaponPicks[(size_t)bestF].available = false; sv_.weaponPicks[(size_t)bestF].respawn = svData_.weaponPickups[(size_t)bestF].respawn; }\n"
     "        } else if (bestD >= 0) {\n"
     "            const Weapon w = sv_.drops[(size_t)bestD].w;\n"
     "            sv_.drops.erase(sv_.drops.begin() + bestD);\n"
     "            svTakeWeapon(*c, (int)q, w);\n"
     "        }\n"
     "    }\n"
     "}\n\n"
     "float World::svZoneScore(int z, int t) const {"),
])
print('ok')
