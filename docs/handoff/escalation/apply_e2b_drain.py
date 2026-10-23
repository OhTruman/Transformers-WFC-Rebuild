# One-off source edit: RE's E2 answers (ESCALATION_E2_ANSWERS_RE.md 319416e) in docs/handoff/escalation/{Survival.h, WorldSurvival.cpp}:
# the base DrainRate 10, drain = int(max(1, dt x DrainRate)) per 30 Hz tick (the 360's rate), _AllowDraining set by a press inside the
# volume (reset on entering, cleared on release), and the item generators' recharge (Taken -> Hide + power back to 0 -> 2 s -> volume on).
import sys
P = 'docs/handoff/escalation/'

def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if s.count(a) != 1: sys.exit(path + ': anchor not unique / missing: ' + a[:80])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Survival.h', [
    ("// TnSurvivalGenerator CDO DrainRate, for a generator authored without one (BrokenHope ShotgunGenerator 12583) [PROV: RE asked].\n"
     "constexpr float kSvDefaultDrainRate = 50.0f;",
     "// TnSurvivalGeneratorBase.DrainRate (no subclass override), for a generator authored without one (BrokenHope 12583) [CONF RE E2 §2].\n"
     "constexpr float kSvDefaultDrainRate = 10.0f;\n"
     "// The 360 ticked at 30 Hz: the per-tick drain int(max(1, dt x DrainRate)) is emulated on that tick (the clamp matters) [RE E2 §3].\n"
     "constexpr float kSvDrainTick = 1.0f / 30.0f;"),
    ("    std::vector<uint8_t> drainFailed;                   // per match player: draining failed this step (no power: the fail sound)\n",
     "    std::vector<uint8_t> drainFailed;                   // per match player: draining failed this step (no power: the fail sound)\n"
     "    std::vector<uint8_t> allowDrain, prevInteract;      // per match player: _AllowDraining (a press inside; reset on entering) / last step's held bit\n"
     "    std::vector<float> drainClock;                      // per match player: time toward the next 30 Hz drain tick\n"
     "    std::vector<int> itemFactory;                       // per generator: the pickup factory it unhides (-1: a door / zone generator)\n"
     "    std::vector<float> reofferTimer;                    // per generator: > 0 while the 2 s Delay before its volume turns back on runs\n"),
])

edit(P + 'WorldSurvival.cpp', [
    ("    sv_.killsSeen = match_.killHistory().size();\n    int hidden = 0;\n",
     "    sv_.killsSeen = match_.killHistory().size();\n"
     "    sv_.allowDrain.assign(match_.players().size(), 0);\n"
     "    sv_.prevInteract.assign(match_.players().size(), 0);\n"
     "    sv_.drainClock.assign(match_.players().size(), 0.0f);\n"
     "    sv_.itemFactory.assign(svData_.generators.size(), -1);\n"
     "    sv_.reofferTimer.assign(svData_.generators.size(), 0.0f);\n"
     "    for (size_t g = 0; g < svData_.generators.size(); ++g)\n"
     "        for (const SurvivalData::Generator::Action& a : svData_.generators[g].actions)\n"
     "            if (a.op == \"unhide\")\n"
     "                for (size_t f = 0; f < pickupFactories_.size(); ++f) if (pickupFactories_[f]->name() == a.target) sv_.itemFactory[g] = (int)f;\n"
     "    int hidden = 0;\n"),
    ("    if (sv_.power.size() < P.size()) { sv_.power.resize(P.size(), 0.0f); sv_.nearGenerator.resize(P.size(), -1); sv_.drainFailed.resize(P.size(), 0); }\n",
     "    if (sv_.power.size() < P.size()) {\n"
     "        sv_.power.resize(P.size(), 0.0f); sv_.nearGenerator.resize(P.size(), -1); sv_.drainFailed.resize(P.size(), 0);\n"
     "        sv_.allowDrain.resize(P.size(), 0); sv_.prevInteract.resize(P.size(), 0); sv_.drainClock.resize(P.size(), 0.0f);\n"
     "    }\n"
     "    // Item generators recharge (map Kismet per generator): PickupStatusChange Taken -> Hide + ModifySurvivalGeneratorPower(-cost) ->\n"
     "    // Delay 2 s -> the volume Turn On; one purchase per charge [RE E2 §1].\n"
     "    for (size_t g = 0; g < svData_.generators.size(); ++g) {\n"
     "        const int f = sv_.itemFactory[g];\n"
     "        if (f >= 0 && sv_.generatorDone[g] && !pickupFactories_[(size_t)f]->hidden() && !pickupFactories_[(size_t)f]->available()) {\n"
     "            pickupFactories_[(size_t)f]->setHidden(true);\n"
     "            sv_.pool[g] = 0.0f; sv_.generatorDone[g] = 0;\n"
     "            sv_.reofferTimer[g] = 2.0f;\n"
     "        }\n"
     "        if (sv_.reofferTimer[g] > 0.0f && (sv_.reofferTimer[g] -= dt) <= 0.0f) { sv_.reofferTimer[g] = 0.0f; sv_.generatorOff[g] = 0; }\n"
     "    }\n"),
    ("    // Generators: a player inside a usable generator's ActivationVolume holding Interact drains max(1, dt x DrainRate) (capped at what\n"
     "    // is still needed and at its power) into the generator's shared pool; at the cost the generator fires its one level.\n"
     "    for (size_t q = 0; q < P.size(); ++q) {\n"
     "        sv_.nearGenerator[q] = -1; sv_.drainFailed[q] = 0;\n"
     "        if (P[q].kind == ParticipantKind::Bot) continue;\n"
     "        const Character* c = participantPawn((int)q);\n"
     "        if (!c) continue;\n"
     "        for (size_t g = 0; g < svData_.generators.size(); ++g)\n"
     "            if (!sv_.generatorDone[g] && !sv_.generatorOff[g] && svData_.generators[g].contains(c->position())) { sv_.nearGenerator[q] = (int)g; break; }\n"
     "        const int g = sv_.nearGenerator[q];\n"
     "        if (g < 0 || !svInteractHeld((int)q)) continue;\n"
     "        const SurvivalData::Generator& G = svData_.generators[(size_t)g];\n"
     "        const float rate = G.drainRate > 0.0f ? G.drainRate : kSvDefaultDrainRate;\n"
     "        const float need = (float)G.cost - sv_.pool[(size_t)g];\n"
     "        const float amt = std::min({std::max(1.0f, dt * rate), need, sv_.power[q]});\n"
     "        if (amt <= 0.0f) { sv_.drainFailed[q] = 1; continue; }   // DrainingFailedSound\n"
     "        sv_.power[q] -= amt; sv_.pool[(size_t)g] += amt;\n"
     "        if (sv_.pool[(size_t)g] >= (float)G.cost - 1e-3f) { sv_.generatorDone[(size_t)g] = 1; svFireGenerator(g); }\n"
     "    }\n",
     "    // Generators (per occupying player pawn, so two players add up, each paying from their own power): _AllowDraining is reset on\n"
     "    // entering the ActivationVolume, set by an Interact press inside, cleared on release; while set, each 30 Hz tick drains\n"
     "    // int(max(1, tick x DrainRate)) capped at the remaining cost and the player's power into the shared pool. 0 drained: the fail\n"
     "    // sound. At the cost the generator fires its one level [RE E2 §3-§4].\n"
     "    for (size_t q = 0; q < P.size(); ++q) {\n"
     "        const int was = sv_.nearGenerator[q];\n"
     "        sv_.nearGenerator[q] = -1; sv_.drainFailed[q] = 0;\n"
     "        const bool held = P[q].kind != ParticipantKind::Bot && svInteractHeld((int)q);\n"
     "        const bool press = held && !sv_.prevInteract[q];\n"
     "        sv_.prevInteract[q] = held ? 1 : 0;\n"
     "        if (P[q].kind == ParticipantKind::Bot) continue;\n"
     "        const Character* c = participantPawn((int)q);\n"
     "        if (!c) { sv_.allowDrain[q] = 0; continue; }\n"
     "        for (size_t g = 0; g < svData_.generators.size(); ++g)\n"
     "            if (!sv_.generatorDone[g] && !sv_.generatorOff[g] && svData_.generators[g].contains(c->position())) { sv_.nearGenerator[q] = (int)g; break; }\n"
     "        const int g = sv_.nearGenerator[q];\n"
     "        if (g != was) { sv_.allowDrain[q] = 0; sv_.drainClock[q] = 0.0f; }   // entering (or leaving): _AllowDraining = false\n"
     "        if (g < 0) continue;\n"
     "        if (press) sv_.allowDrain[q] = 1;\n"
     "        if (!held) sv_.allowDrain[q] = 0;\n"
     "        if (!sv_.allowDrain[q]) continue;\n"
     "        const SurvivalData::Generator& G = svData_.generators[(size_t)g];\n"
     "        const float rate = G.drainRate > 0.0f ? G.drainRate : kSvDefaultDrainRate;\n"
     "        for (sv_.drainClock[q] += dt; sv_.drainClock[q] >= kSvDrainTick - 1e-6f && !sv_.generatorDone[(size_t)g]; sv_.drainClock[q] -= kSvDrainTick) {\n"
     "            const float need = (float)G.cost - sv_.pool[(size_t)g];\n"
     "            const float amt = std::min({std::floor(std::max(1.0f, kSvDrainTick * rate)), need, std::floor(sv_.power[q])});\n"
     "            if (amt <= 0.0f) { sv_.drainFailed[q] = 1; break; }   // DrainingFailedSound, beam off\n"
     "            sv_.power[q] -= amt; sv_.pool[(size_t)g] += amt;\n"
     "            if (sv_.pool[(size_t)g] >= (float)G.cost) { sv_.generatorDone[(size_t)g] = 1; svFireGenerator(g); }\n"
     "        }\n"
     "    }\n"),
])
print('ok')
