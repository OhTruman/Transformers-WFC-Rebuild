# One-off source edit (RE E2b1 review W1): the terminal window's arming follows the players' Touched / UnTouched events on TV 8879
# (per pawn: an enter arms + makes it vulnerable, a leave disarms + invulnerable), and the window is invulnerable to everything while
# disarmed (SeqAct_TnToggleDestructible; PreGameplaySetup sets it invulnerable). Run from the Gameplay worktree root.
import sys
P = 'docs/handoff/escalation/'

def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if s.count(a) != 1: sys.exit(path + ': anchor not unique / missing: ' + a[:80])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Survival.h', [
    ("    bool windowArmed = false, windowDone = false;\n",
     "    bool windowArmed = false, windowDone = false;\n"
     "    std::vector<uint8_t> windowInside;                    // per match player: inside the arming volume last step\n"),
])
edit(P + 'WorldSurvival.cpp', [
    ("    sv_.crashDone.assign(svData_.crashWalls.size(), 0);\n",
     "    sv_.crashDone.assign(svData_.crashWalls.size(), 0);\n"
     "    sv_.windowInside.assign(match_.players().size(), 0);\n"
     "    if (svData_.window.volume >= 0)   // PreGameplaySetup: SeqAct_TnToggleDestructible 1 -> _Invulnerable\n"
     "        for (Destructible* d : destructibles_) {\n"
     "            const std::string& n = d->name(); const std::string& w = svData_.window.wall;\n"
     "            if (n.size() >= w.size() && n.compare(n.size() - w.size(), w.size(), w) == 0) d->setInvulnerable(true);\n"
     "        }\n"),
    ("        if (!sv_.windowArmed && anyInside(w.enable, 1)) sv_.windowArmed = true;   // a player arms the AI-only touch\n",
     "        // TV 8879 per player pawn event: Touched -> the AI touch on + vulnerable; UnTouched -> off + invulnerable [RE review W1].\n"
     "        if (sv_.windowInside.size() < P.size()) sv_.windowInside.resize(P.size(), 0);\n"
     "        for (size_t q = 0; q < P.size(); ++q) {\n"
     "            const Character* c = P[q].kind != ParticipantKind::Bot ? participantPawn((int)q) : nullptr;\n"
     "            const uint8_t in = c && w.enable >= 0 && svData_.volumes[(size_t)w.enable].second.contains(c->position()) ? 1 : 0;\n"
     "            if (in != sv_.windowInside[q]) {\n"
     "                sv_.windowArmed = in != 0;\n"
     "                for (Destructible* d : destructibles_) {\n"
     "                    const std::string& n = d->name();\n"
     "                    if (n.size() >= w.wall.size() && n.compare(n.size() - w.wall.size(), w.wall.size(), w.wall) == 0) d->setInvulnerable(!sv_.windowArmed);\n"
     "                }\n"
     "            }\n"
     "            sv_.windowInside[q] = in;\n"
     "        }\n"),
])
print('ok')
