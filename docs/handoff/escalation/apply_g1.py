# One-off source edit (RE E2a review G1, Integration: faithful): the item generator's PickupStatusChange Taken chain run literally on
# every take - Hide; ModifySurvivalGeneratorPower(-cost); a level drop (1 -> 0) fires "New Power Level" again (the event fires on both
# directions), re-applying the actions (UnHide + the volume off); Delay 2 s -> the volume on. Run from the Gameplay worktree root.
import sys
P = 'docs/handoff/escalation/'

def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if s.count(a) != 1: sys.exit(path + ': anchor not unique / missing: ' + a[:80])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Survival.h', [
    ("    std::vector<float> reofferTimer;                    // per generator: > 0 while the 2 s Delay before its volume turns back on runs\n",
     "    std::vector<float> reofferTimer;                    // per generator: > 0 while the 2 s Delay before its volume turns back on runs\n"
     "    std::vector<uint8_t> factoryWasAvailable;           // per generator: its factory's available() last step (the Taken edge)\n"),
])
edit(P + 'WorldSurvival.cpp', [
    ("    sv_.reofferTimer.assign(svData_.generators.size(), 0.0f);\n",
     "    sv_.reofferTimer.assign(svData_.generators.size(), 0.0f);\n"
     "    sv_.factoryWasAvailable.assign(svData_.generators.size(), 1);\n"),
    ("    // Item generators recharge (map Kismet per generator): PickupStatusChange Taken -> Hide + ModifySurvivalGeneratorPower(-cost) ->\n"
     "    // Delay 2 s -> the volume Turn On; one purchase per charge [RE E2 §1].\n"
     "    for (size_t g = 0; g < svData_.generators.size(); ++g) {\n"
     "        const int f = sv_.itemFactory[g];\n"
     "        if (f >= 0 && sv_.generatorDone[g] && !pickupFactories_[(size_t)f]->hidden() && !pickupFactories_[(size_t)f]->available()) {\n"
     "            pickupFactories_[(size_t)f]->setHidden(true);\n"
     "            sv_.pool[g] = 0.0f; sv_.generatorDone[g] = 0;\n"
     "            sv_.reofferTimer[g] = 2.0f;\n"
     "        }\n",
     "    // Item generators (map Kismet per generator, PickupStatusChange Taken): Hide; ModifySurvivalGeneratorPower(-cost); a level drop\n"
     "    // (1 -> 0) fires \"New Power Level\" again - the event fires on negative changes too - so its actions re-apply (UnHide + the volume\n"
     "    // off); Delay 2 s -> the volume Turn On. After the first purchase the item thus stays out as a normal respawning factory [RE E2a\n"
     "    // review G1, C]; a take at level 0 only hides it until the generator is bought again.\n"
     "    for (size_t g = 0; g < svData_.generators.size(); ++g) {\n"
     "        const int f = sv_.itemFactory[g];\n"
     "        if (f < 0) continue;\n"
     "        PickupFactory& F = *pickupFactories_[(size_t)f];\n"
     "        const bool avail = F.available() && !F.hidden();\n"
     "        if (sv_.factoryWasAvailable[g] && !avail && !F.hidden()) {   // Taken\n"
     "            F.setHidden(true);\n"
     "            const bool drop = sv_.generatorDone[g] != 0;\n"
     "            sv_.pool[g] = std::max(0.0f, sv_.pool[g] - (float)svData_.generators[g].cost);\n"
     "            sv_.generatorDone[g] = 0;\n"
     "            if (drop) svFireGenerator((int)g, true);\n"
     "            sv_.reofferTimer[g] = 2.0f;\n"
     "        }\n"
     "        sv_.factoryWasAvailable[g] = (F.available() && !F.hidden()) ? 1 : 0;\n"),
    ("void World::svFireGenerator(int gi) {\n",
     "void World::svFireGenerator(int gi, bool levelDrop) {\n"),
    ("    ++sv_.purchases;\n"
     "    LOG_INFO(\"escalation: generator %s (%s, %d) bought: %d actions applied, %d not built yet (E2b)\", g.id.c_str(), g.nameKey.c_str(), g.cost, done, pending);\n",
     "    if (!levelDrop) ++sv_.purchases;\n"
     "    LOG_INFO(\"escalation: generator %s (%s, %d) %s: %d actions applied, %d not built yet (E2b)\", g.id.c_str(), g.nameKey.c_str(), g.cost,\n"
     "             levelDrop ? \"level drop (Taken)\" : \"bought\", done, pending);\n"),
])
print('ok')
