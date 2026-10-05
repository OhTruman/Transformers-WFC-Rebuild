// Clean-room reconstruction — headless frontend / game-flow tests (no window, renderer or audio).
// Run: build/bin/wfc_frontend_tests.exe   (reads the AssetTools frontend manifests; WFC_FRONTEND_MANIFESTS overrides)
// Exit code = number of failed checks. Every check prints "PASS|FAIL <name> ..." for the Experimental harness.
#include "frontend/Catalog.h"
#include "frontend/FrontendRuntime.h"
#include "frontend/GameFlow.h"
#include "frontend/UIController.h"
#include "frontend/Url.h"
#include "core/Config.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace frontend;

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* name, const std::string& detail = "") {
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
    (ok ? g_pass : g_fail)++;
}

static void testUrl() {
    Url u = Url::parse("UI_Lobby_m?Game=TransContent.TnGameLobbyGameTeam?GameModeTag=TDM?GameTeamStatus=3?listen");
    check(u.map() == "UI_Lobby_m", "url.map");
    check(u.option("gamemodetag") == "TDM", "url.option_case_insensitive");
    check(u.intOption("GameTeamStatus", -1) == 3, "url.intOption");
    check(u.hasOption("listen") && u.option("listen").empty(), "url.flag");
    check(u.toString() == "UI_Lobby_m?Game=TransContent.TnGameLobbyGameTeam?GameModeTag=TDM?GameTeamStatus=3?listen", "url.roundtrip");
    check(u.intOption("MapId", -1) == -1, "url.intOption_default");
}

static void testCatalog(const Catalog& c) {
    check(c.maps().size() == 13, "catalog.map_providers", std::to_string(c.maps().size()));
    bool ordered = true;
    for (size_t i = 1; i < c.maps().size(); ++i) ordered &= c.maps()[i - 1].iniOrder <= c.maps()[i].iniOrder;
    check(ordered, "catalog.map_order_ini");
    const MapInfo* s = c.mapById(508);
    check(s && s->mapFilename == "MP_IAC_Streets_Base_m" && s->friendlyName == "Streets", "catalog.streets_508");
    check(s && s->runtimeDir == "MP_IAC_Streets" && s->hasRequiredAssets, "catalog.streets_runtime_data");
    check(s && s->compatibleWith("TDM") && s->compatibleWith("DOM") && !s->compatibleWith("SV"), "catalog.streets_modes");
    check(s && s->thumbnailPng == "content/UI_LevelThumbnails_p/MP_Streets.png", "catalog.streets_thumbnail");
    const MapInfo* f = c.mapById(512);
    check(f && !f->cooked && !f->hasRequiredAssets, "catalog.dlc_fortress_disabled");
    std::string order;
    for (const Playlist& p : c.playlists()) order += p.tag + " ";
    check(order == "TDM DM DOM CTF EXT KOTH SV CP CCP ", "catalog.playlist_order", order);
    check(c.defaultQuickmatchPlaylistId() == 1, "catalog.default_quickmatch_playlist");
    const GameSettings* t = c.settingsForTag("TDM", true);
    check(t && t->className == "TnOnlineGameSettingsTDMPrivate" && t->mapSelectionMethod == 1, "catalog.tdm_private_settings");
    check(t && t->rules.size() == 4 && t->rules[0] == "TransGame.TnGameRules_ScoreKillsTDM", "catalog.tdm_rules_head");
    check(t && t->timeLimits.size() == 3 && t->timeLimits[1] == 900 && t->pointsToWin == 40, "catalog.tdm_score_time");
    check(c.modeFriendlyName("TDM") == "Team Deathmatch" && c.modeFriendlyName("DOM") == "Conquest", "catalog.mode_names");
    check(c.localize("UIText", "LoadScreen", "LoadingMap") == "in `m", "catalog.loc_loadingmap");
    check(c.localizeKey("$UIText.MainMenu.PressStart").size() > 0, "catalog.loc_gfx_key", c.localizeKey("$UIText.MainMenu.PressStart"));
    check(c.engageTexts().size() == 26, "catalog.engage_texts_base", std::to_string(c.engageTexts().size()));
    // Selectable = cooked + rebuild runtime world + render export on disk (grows as AssetTools exports maps).
    auto tdmMaps = c.compatibleMaps("TDM", true);
    size_t onDisk = 0;
    const std::string mapsRoot = (std::getenv("WFC_ASSETS") ? std::string(std::getenv("WFC_ASSETS")) : std::string(core::config::kAssetRootDefault)) + "/Maps/";
    for (const MapInfo& m : c.maps()) onDisk += m.cooked && m.compatibleWith("TDM") && std::ifstream(mapsRoot + m.runtimeDir + "/world.glb").good()
                                                && (std::ifstream(mapsRoot + m.runtimeDir + "/render_index.json").good()
                                                    || std::ifstream(Catalog::defaultManifestRoot() + "/maps/" + m.runtimeDir + "/render_index_generic.json").good());
    bool hasStreets = false;
    for (auto* m : tdmMaps) hasStreets |= m->mapId == 508;
    check(hasStreets && tdmMaps.size() == onDisk, "catalog.tdm_selectable_maps_match_disk", std::to_string(tdmMaps.size()));
}

static void testUIController() {
    UIController ui;
    std::string last;
    ui.initialize(UIControllerClass::FrontEnd, [&](const std::string& m) { last = m; }, [] {}, [](bool) {}, nullptr);
    check(ui.state() == UIState::NotInGame, "ui.frontend_starting_state");
    ui.onUIEvent(0);
    check(ui.state() == UIState::FrontEnd && last == "UI_GFxFrontEnd_p.FrontEnd_GFX_1", "ui.event0_frontend");
    ui.initialize(UIControllerClass::GameLobby, [&](const std::string& m) { last = m; }, [] {}, [](bool) {}, nullptr);
    check(ui.state() == UIState::InLobby && last == "UI_GFxLobbies_p.GameLobby_GFX_1", "ui.gamelobby_inlobby");
    bool hud = false;
    ui.initialize(UIControllerClass::Multiplayer, [&](const std::string& m) { last = m; }, [] {}, [&](bool v) { hud = v; }, nullptr);
    check(ui.state() == UIState::WaitingOnGameStart && last != "UI_GFxCustomize_p.CustomTransformers_GFX_1", "ui.mp_waiting_without_gri");
    ui.setGRIAvailable(true);
    check(last == "UI_GFxCustomize_p.CustomTransformers_GFX_1", "ui.mp_ingamelobbyui");
    ui.onCharacterSelected(false);
    check(last == "UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1", "ui.mp_gamestartui");
    ui.onUIEvent(3);
    check(ui.state() == UIState::InGame && hud, "ui.event3_ingame_hud");
    ui.onUIEvent(6);
    check(ui.state() == UIState::Paused && last == "UI_GFxPause_p.PauseMenu_GFX_1", "ui.event6_pause");
    ui.onCurrentUIClosed();
    check(ui.state() == UIState::InGame, "ui.pause_closed_resumes");
    ui.onUIEvent(4);
    check(ui.state() == UIState::Spectating && last == "UI_GFxRespawn_p.MultiplayerRespawn_GFX_1", "ui.event4_spectating");
    ui.onUIEvent(5);
    ui.onUIEvent(9);
    check(ui.state() == UIState::GameEnded && !hud && last == "UI_GFxEndGameStats_p.EndGameStats_GFX_1", "ui.event9_gameended");
    ui.onUIEvent(10);
    check(ui.state() == UIState::WaitingOnGameStart, "ui.event10_playagain");
    ui.onUIEvent(3);
    ui.onUIEvent(6);
    ui.onUIEvent(11);
    check(ui.state() == UIState::NotInGame, "ui.event11_gotomainmenu");

    // UseInGameLobby (no character selected yet): OnBeginGame is ignored; spawn (OnRespawn) enters InGame and closes
    // the open pre-game screen; leaving InGame for the pause menu hides the HUD (InGame.EndState).
    UIController ui2;
    std::string open2;
    bool hud2 = false;
    ui2.initialize(UIControllerClass::Multiplayer, [&](const std::string& m) { open2 = m; }, [&] { open2.clear(); }, [&](bool v) { hud2 = v; }, nullptr, true);
    ui2.setGRIAvailable(true);
    ui2.onUIEvent(3);
    check(ui2.state() == UIState::WaitingOnGameStart && open2 == "UI_GFxCustomize_p.CustomTransformers_GFX_1", "ui.ingamelobby_ignores_begingame");
    ui2.onCharacterSelected(true);
    ui2.onUIEvent(5);
    check(ui2.state() == UIState::InGame && hud2 && open2.empty() && ui2.openMovie().empty(), "ui.spawn_closes_choose_character");
    ui2.onUIEvent(6);
    check(ui2.state() == UIState::Paused && !hud2, "ui.pause_hides_hud");
    ui2.onCurrentUIClosed();
    check(ui2.state() == UIState::InGame && hud2, "ui.resume_shows_hud");
}

// Runs the runtime (flow + shims + script) until `pred` or a frame budget.
template <class P> static bool runUntil(FrontendRuntime& rt, P pred, int maxFrames = 2000) {
    platform::InputFrame in;
    for (int i = 0; i < maxFrames; ++i) {
        if (pred()) return true;
        rt.update(in, 1.0f / 60.0f);
    }
    return pred();
}

static void testFlow() {
    // Fresh profile: the intro movies chain runs first (no decoder: each reports Stopped).
    std::remove("wfc_profile.ini");
#ifdef _WIN32
    _putenv("WFC_FRONTEND_AUTOPLAY=TDM,508");
    _putenv("WFC_FLOWSEED=11");
#else
    setenv("WFC_FRONTEND_AUTOPLAY", "TDM,508", 1);
    setenv("WFC_FLOWSEED", "11", 1);
#endif
    FrontendRuntime rt;
    check(rt.init(), "flow.init");
    GameFlow& f = rt.flow();
    check(runUntil(rt, [&] { return f.frontEndStarted(); }), "flow.frontend_started");
    check(f.ui().state() == UIState::FrontEnd && f.ui().openMovie() == "UI_GFxFrontEnd_p.FrontEnd_GFX_1", "flow.frontend_ui");
    check(runUntil(rt, [&] { return f.level() == LevelKind::GameLobby && !f.loading().active; }), "flow.reached_gamelobby");
    check(f.lobby().gameModeTag == "TDM" && f.lobby().mapSelectionMethod == 1, "flow.gamelobby_private_tdm");
    check(runUntil(rt, [&] { return f.hasPendingMatch(); }, 60 * 20), "flow.match_launch_after_countdown");
    const std::string expected = "MP_IAC_Streets_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame"
                                 "?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters="
                                 "?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=508";
    check(f.pendingMatch().url.toString() == expected, "flow.match_url_equals_RE_3.1", f.pendingMatch().url.toString());
    check(f.pendingMatch().goalScore == 40 && f.pendingMatch().timeLimit == 900, "flow.match_url_readback");
    check(f.pendingMatch().map && f.pendingMatch().map->runtimeDir == "MP_IAC_Streets", "flow.match_runtime_dir");
    check(f.pendingMatch().teamIndex == 0 || f.pendingMatch().teamIndex == 1, "flow.match_team_assigned");
    check(f.loading().active && f.loading().kind == "Map" && f.loading().title == "Team Deathmatch" && f.loading().message == "in Streets",
          "flow.loading_map_text", f.loading().title + " / " + f.loading().message);
    check(f.loading().engageTexts.size() == 3, "flow.loading_three_engage_texts");
    f.matchLoaded();
    check(f.level() == LevelKind::Match && !f.loading().active && f.ui().cls() == UIControllerClass::Multiplayer, "flow.match_level");
    // New match PRI: UseInGameLobby = !HasSelectedCharacter() -> OnBeginGame alone does not leave Choose Character.
    f.onUIEvent(3);
    check(f.ui().state() == UIState::WaitingOnGameStart && f.ui().openMovie() == "UI_GFxCustomize_p.CustomTransformers_GFX_1",
          "flow.match_start_keeps_choose_character");
    f.characterSelected();
    f.onUIEvent(5);   // the pawn spawns: OnRespawn -> InGame, WaitingOnGameStart.EndState closes the screen
    check(f.ui().state() == UIState::InGame && f.ui().hudVisible() && f.ui().openMovie().empty(), "flow.match_ingame");
    f.showMenu();
    check(f.ui().state() == UIState::Paused && f.ui().openMovie() == "UI_GFxPause_p.PauseMenu_GFX_1", "flow.showmenu_pause");
    f.call("Game.QuitToMainMenu");
    check(f.wantsWorldUnload(), "flow.quit_unloads_world");
    f.worldUnloaded();
    check(runUntil(rt, [&] { return f.level() == LevelKind::FrontEnd && f.ui().state() == UIState::FrontEnd; }), "flow.returned_to_frontend");
    check(f.hasWatchedIntroMovie(), "flow.intro_marked_watched");
    std::remove("wfc_profile.ini");
}

static void testCatalogExtensibility(const std::string& manifests, const std::string& extracted) {
    // A newly recovered map: runtime data appears for MP_IAC_Rust -> it becomes selectable for its modes, with no
    // code change (the provider list itself is the shipped TransLevels.ini set).
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "wfc_frontend_test_maps";
    fs::create_directories(tmp / "MP_IAC_Streets");
    fs::create_directories(tmp / "MP_IAC_Rust");
    std::ofstream(tmp / "MP_IAC_Streets" / "world.glb") << "x";
    std::ofstream(tmp / "MP_IAC_Rust" / "world.glb") << "x";
    // A selectable map also has the AssetTools render export (integration M05 selectability gate).
    std::ofstream(tmp / "MP_IAC_Streets" / "render_index.json") << "{}";
    std::ofstream(tmp / "MP_IAC_Rust" / "render_index.json") << "{}";
    Catalog c;
    check(c.load(manifests, extracted, tmp.string()), "extensibility.load");
    const MapInfo* r = c.mapById(504);
    check(r && r->hasRequiredAssets && r->runtimeDir == "MP_IAC_Rust", "extensibility.rust_enabled");
    auto tdm = c.compatibleMaps("TDM", true);
    check(tdm.size() == 2 && tdm[0]->mapId == 504 && tdm[1]->mapId == 508, "extensibility.tdm_maps_in_ini_order", std::to_string(tdm.size()));
    check(c.compatibleMaps("SV", true).empty(), "extensibility.sv_no_runtime_maps");
    fs::remove_all(tmp);
}

int main() {
    std::string vs = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    Catalog c;
    bool ok = c.load(Catalog::defaultManifestRoot(), Catalog::defaultExtractedRoot(), vs + "/Maps");
    check(ok, "catalog.load");
    testUrl();
    if (ok) testCatalog(c);
    testUIController();
    if (ok) testFlow();
    if (ok) testCatalogExtensibility(Catalog::defaultManifestRoot(), Catalog::defaultExtractedRoot());
    std::printf("frontend tests: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail;
}
