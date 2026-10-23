# One-off source edit (E2b part 3): the Escalation nav. In GameModeTag ESC the director loads AssetTools'
# manifests/maps/<map>/bot_nav_escalation.json (one cell per ORIGINAL NavigationCell), blocks escalation_nav_blockers.json's
# blocked_at_start cells and releases each generator's opened_by_generator cells on its purchase - the original's own gating, replacing
# the door-box blocking. A non-ESC launch puts the versus nav back. Run from the Gameplay worktree root.
import sys
P = 'docs/handoff/escalation/'

def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if s.count(a) != 1: sys.exit(path + ': anchor not unique / missing: ' + a[:80])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Survival.h', [
    ("    int roundBegins = 0, roundEnds = 0;\n",
     "    int roundBegins = 0, roundEnds = 0;\n"
     "    // The Escalation nav (E2b part 3): loaded for this match; each generator's NavigationCells (opened on purchase); start-blocked count.\n"
     "    bool escNav = false;\n"
     "    std::vector<std::pair<std::string, std::vector<int>>> generatorCells;\n"
     "    int navBlockedAtStart = 0;\n"),
])

edit(P + 'WorldSurvival.cpp', [
    ("void World::setupSurvival() {\n    sv_ = SurvivalState{};\n    if (!match_.settings().survival) return;\n",
     "// The Escalation nav file / blockers path (AssetTools manifests; WFC_MANIFESTS overrides, as the other manifest readers).\n"
     "static std::string svManifest(const std::string& map, const char* file) {\n"
     "    const char* mr = std::getenv(\"WFC_MANIFESTS\");\n"
     "    return std::string(mr ? mr : \"F:/Transformers Rebuild/AssetTools/manifests\") + \"/maps/\" + map + \"/\" + file;\n"
     "}\n\n"
     "void World::setupSurvival() {\n"
     "    const bool hadEscNav = sv_.escNav;\n"
     "    sv_ = SurvivalState{};\n"
     "    if (!match_.settings().survival) {\n"
     "        if (hadEscNav) { botNavTried_ = false; ensureBotNav(); LOG_INFO(\"escalation: the versus nav is back for %s\", mapName_.c_str()); }\n"
     "        return;\n"
     "    }\n"),
    ("    ensureBotNav();\n    const int horde = svData_.coopTeam == 0 ? 1 : 0;\n",
     "    ensureBotNav();\n"
     "    {   // E2b part 3: the original NavigationCell mesh and its gating [AssetTools from escalation_navmesh.json + the generators' Kismet]\n"
     "        BotNav esc;\n"
     "        if (esc.load(svManifest(mapName_, \"bot_nav_escalation.json\"))) {\n"
     "            botNav_ = std::move(esc);\n"
     "            sv_.escNav = true;\n"
     "            std::ifstream f(svManifest(mapName_, \"escalation_nav_blockers.json\"), std::ios::binary);\n"
     "            std::stringstream ss; ss << f.rdbuf();\n"
     "            assets::Json b;\n"
     "            if (f.good() && assets::Json::parse(ss.str(), b)) {\n"
     "                for (size_t k = 0; k < b[\"blocked_at_start\"].size(); ++k) { botNav_.setBlocked(b[\"blocked_at_start\"][k].asInt(-1), true); ++sv_.navBlockedAtStart; }\n"
     "                for (const auto& g : b[\"opened_by_generator\"].obj) {\n"
     "                    std::vector<int> cells;\n"
     "                    for (size_t k = 0; k < g.second.size(); ++k) cells.push_back(g.second[k].asInt(-1));\n"
     "                    sv_.generatorCells.push_back({g.first, cells});\n"
     "                }\n"
     "            }\n"
     "            botNav_.recomputePieces();\n"
     "            LOG_INFO(\"escalation: nav %s: %zu cells, %d blocked at start, %zu generators open cells\", \"bot_nav_escalation.json\", botNav_.cells().size(),\n"
     "                     sv_.navBlockedAtStart, sv_.generatorCells.size());\n"
     "        } else LOG_WARN(\"escalation: no bot_nav_escalation.json for %s - the versus nav (PROV)\", mapName_.c_str());\n"
     "    }\n"
     "    const int horde = svData_.coopTeam == 0 ? 1 : 0;\n"),
    ("    if (doors && botNav_.valid()) { transforming_.applyNavBlocks(botNav_); botNav_.recomputePieces(); }   // an opened doorway opens the nav\n",
     "    if (sv_.escNav) {   // the original's gating: this generator's SeqAct_Toggle Turn On NavigationCells\n"
     "        bool opened = false;\n"
     "        for (const auto& gc : sv_.generatorCells)\n"
     "            if (gc.first == \"TnSurvivalGenerator_\" + g.id) { for (int c : gc.second) botNav_.setBlocked(c, false); opened = true; }\n"
     "        if (opened) botNav_.recomputePieces();\n"
     "    } else if (doors && botNav_.valid()) { transforming_.applyNavBlocks(botNav_); botNav_.recomputePieces(); }   // versus nav: door boxes\n"),
])
s = open(P + 'WorldSurvival.cpp', encoding='utf-8').read()
for inc in ('#include <fstream>\n', '#include <sstream>\n'):
    if inc not in s: s = s.replace('#include <cmath>\n', '#include <cmath>\n' + inc, 1)
open(P + 'WorldSurvival.cpp', 'w', encoding='utf-8', newline='\n').write(s)

edit(P + 'Application_EscTest.cpp', [
    ("    check(D.weaponPickups.size() == 17, \"17 weapon pickup factories (hidden until bought)\");\n",
     "    check(D.weaponPickups.size() == 17, \"17 weapon pickup factories (hidden until bought)\");\n"
     "    check(S.escNav && world_.botNav().cells().size() == 469 && S.navBlockedAtStart == 32 && S.generatorCells.size() == 12,\n"
     "          \"the Escalation nav: 469 original NavigationCells, 32 blocked at start, 12 generators open cells\");\n"),
    ("        check(t > 3.2f && t < 3.5f && doors && zone, \"Door generator 4199: ~3.33 s (3 per tick), doors 11450 / 9615 transform, zone 10523 activated\");\n",
     "        check(t > 3.2f && t < 3.5f && doors && zone, \"Door generator 4199: ~3.33 s (3 per tick), doors 11450 / 9615 transform, zone 10523 activated\");\n"
     "        bool open = false;\n"
     "        for (const auto& gc : S.generatorCells) if (gc.first == \"TnSurvivalGenerator_4199\") { open = !gc.second.empty(); for (int c : gc.second) open = open && !world_.botNav().isBlocked(c); }\n"
     "        check(open, \"Door generator 4199: its NavigationCells are released (the original's Turn On)\");\n"),
])
print('ok')
