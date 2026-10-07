// Clean-room reconstruction — headless frontend / game-flow tests (no window, renderer or audio).
// Run: build/bin/wfc_frontend_tests.exe   (reads the AssetTools frontend manifests; WFC_FRONTEND_MANIFESTS overrides)
// Exit code = number of failed checks. Every check prints "PASS|FAIL <name> ..." for the Experimental harness.
#include "frontend/Catalog.h"
#include "frontend/FrontendRuntime.h"
#include "frontend/FrontendScene.h"
#include "frontend/GameFlow.h"
#include "frontend/Hud.h"
#include "frontend/Profile.h"
#include "frontend/UIController.h"
#include "frontend/Url.h"
#include "core/Config.h"

#include <algorithm>
#include <cmath>
#include <sstream>
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

static void testProfileBotMigration() {
    // A profile saved by 09b (8c2b6e3): only BotsFriendly / BotsEnemy under [PCSettings].
    std::istringstream old("[PCSettings]\nWidth=1920\nHeight=1080\nBotsFriendly=3\nBotsEnemy=4\nBotDifficulty=1\n");
    frontend::LocalProfile p;
    const bool migrated = p.loadFrom(old);
    check(migrated && p.bots.autobot == 3 && p.bots.decepticon == 4 && !p.bots.extended, "profile.bots_migrated_from_09b",
          std::to_string(p.bots.autobot) + "/" + std::to_string(p.bots.decepticon));
    std::istringstream big("[PCSettings]\nBotsFriendly=9\nBotsEnemy=9\n");
    frontend::LocalProfile q;
    q.loadFrom(big);
    check(q.bots.autobot == 4 && q.bots.decepticon == 5, "profile.bots_migration_original_limits");
    std::istringstream cur("[PCSettings]\nBotsFriendly=3\nBotsEnemy=4\nBotsAutobot=1\nBotsDecepticon=2\n");
    frontend::LocalProfile c;
    const bool again = c.loadFrom(cur);
    check(!again && c.bots.autobot == 1 && c.bots.decepticon == 2, "profile.bots_current_keys_kept");
}

static void testRecommendedBots() {
    using GF = frontend::GameFlow;
    // Team mode, human on the Autobot side (unpicked): 10 per side -> Autobot 9 bots, Decepticon 10.
    GF::BotCounts t = GF::recommendedBots(10, 15, true, 0, 31, 32, 63);
    check(t.autobot == 9 && t.decepticon == 10, "bots.recommended_team", std::to_string(t.autobot) + "/" + std::to_string(t.decepticon));
    // Human on the Decepticon side.
    GF::BotCounts d = GF::recommendedBots(10, 15, true, 1, 32, 31, 63);
    check(d.autobot == 10 && d.decepticon == 9, "bots.recommended_team_decepticon");
    // FFA: total includes the human.
    GF::BotCounts f = GF::recommendedBots(10, 15, false, 0, 31, 32, 63);
    check(f.enemy == 14, "bots.recommended_ffa", std::to_string(f.enemy));
    // Capacity caps win (never above Gameplay's limits).
    GF::BotCounts c = GF::recommendedBots(40, 80, true, 0, 31, 32, 63);
    GF::BotCounts cf = GF::recommendedBots(40, 80, false, 0, 31, 32, 63);
    check(c.autobot == 31 && c.decepticon == 32 && cf.enemy == 63, "bots.recommended_caps");
    // The "edited since the last map change" flag round-trips through the profile.
    std::istringstream in("[PCSettings]\nBotsAutobot=5\nBotsDecepticon=6\nBotsExtended=1\nBotsEdited=1\n");
    frontend::LocalProfile p;
    p.loadFrom(in);
    check(p.bots.extended && p.bots.editedSinceMap && p.bots.autobot == 5, "bots.edited_flag_persists");
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

// A presenter that records the HUD calls (name + args as text).
struct RecordingPresenter : frontend::IMoviePresenter {
    std::vector<std::string> calls;
    bool runsMovie(const std::string&) const override { return true; }
    void update(frontend::GameFlow&, const platform::InputFrame&, float) override {}
    void draw(const frontend::GameFlow&, int, int) override {}
    void hudCall(const std::string& fn, const std::vector<frontend::BridgeValue>& args) override {
        std::string s = fn + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (i) s += ",";
            if (a.kind == frontend::BridgeValue::Kind::String) s += a.s;
            else if (a.kind == frontend::BridgeValue::Kind::Bool) s += a.b ? "true" : "false";
            else { char b[32]; std::snprintf(b, sizeof b, "%g", a.n); s += b; }
        }
        calls.push_back(s + ")");
    }
    bool has(const std::string& c) const { return std::find(calls.begin(), calls.end(), c) != calls.end(); }
    int count(const std::string& prefix) const { int n = 0; for (const auto& c : calls) n += c.rfind(prefix, 0) == 0; return n; }
};

static void testHudObservers(const Catalog& c) {
    using frontend::HudController; using frontend::HudFrame;
    HudController hud;
    RecordingPresenter p;
    HudFrame f;
    f.valid = true;
    hud.setFrame(f);
    hud.update(&p, c, true, true);
    check(p.count("_global.NotifyProgressBarChanged") == 0 && p.count("_global.NotifyKillstreakChanged") == 0, "hud.unset_observers_send_nothing");
    // progress bar: the observer's TransGame.int label, 0..1
    f.progressObserver = "TnHudDataObserverDominationCapture";
    f.progress = 0.25;
    f.attackingTeamStatus = HudController::attackingStatus(1, 1);
    f.killstreakId = std::string("HealthRegenStreak");
    f.abilities[1] = HudFrame::Ability{"TnAbilityHover", 12.0, 0.4};
    f.grenadeAmmo = 2; f.grenadeType = 1;
    hud.setFrame(f);
    hud.damageIndicator(1.5, 20.0);
    hud.causedDamage();
    p.calls.clear();
    hud.update(&p, c, true, true);
    check(p.has("_global.NotifyProgressBarChanged(Capturing Node,0.25)"), "hud.progress_bar_label", p.calls.empty() ? "" : p.calls[0]);
    check(p.has("_global.NotifyOnAttackingTeamChanged(1)") && HudController::attackingStatus(0, 1) == 2 && HudController::attackingStatus(-1, 1) == 0,
          "hud.attacking_team_status");
    check(p.has("_global.NotifyKillstreakChanged(HealthRegenStreak)") && p.has("_global.NotifyAbilityType1Changed(TnAbilityHover)") &&
          p.has("_global.NotifyAbilityType1UpdateCooldown(12,0.4)") && p.has("_global.NotifyGrenadeAmmoChanged(2,1)"), "hud.killstreak_ability_grenade");
    check(p.has("_global.NotifyDamageIndicatorAdded(1.5,20)") && p.has("_global.CausedDamage()"), "hud.damage_events");
    // unchanged: nothing re-sent; a change: only that value
    p.calls.clear();
    hud.update(&p, c, true, true);
    check(p.count("_global.Notify") == 0 && p.count("_global.CausedDamage") == 0, "hud.observers_change_driven");
    f.progress = 0.0;
    f.progressObserver = "TnHudDataObserverReviveBuddy"; f.progressName = "Bumblebee";
    f.progress = 0.5;
    hud.setFrame(f);
    p.calls.clear();
    hud.update(&p, c, true, true);
    check(p.has("_global.NotifyProgressBarChanged(Reviving Bumblebee,0.5)") && p.calls.size() == 1, "hud.progress_revive_name",
          p.calls.empty() ? "" : p.calls[0]);
    // the bar ends without an observer: the label stays for the fade-out
    f.progressObserver.reset(); f.progress = 0.0;
    hud.setFrame(f); p.calls.clear(); hud.update(&p, c, true, true);
    check(p.has("_global.NotifyProgressBarChanged(Reviving Bumblebee,0)"), "hud.progress_end_keeps_label", p.calls.empty() ? "" : p.calls[0]);
    // prompts diffed into add / remove; refused transforms per increment; conversions
    f.contextualPrompts = std::vector<std::string>{"Pick up"};
    f.cantTransformCount = 0;
    hud.setFrame(f); p.calls.clear(); hud.update(&p, c, true, true);
    f.contextualPrompts = std::vector<std::string>{"Swap"};
    f.cantTransformCount = 2;
    hud.setFrame(f); p.calls.clear(); hud.update(&p, c, true, true);
    check(p.has("_global.NotifyContextualCommand(0,1,Pick up)") && p.has("_global.NotifyContextualCommand(0,0,Swap)") &&
          p.count("_global.NotifyCantTransform") == 2, "hud.prompts_and_cant_transform");
    check(HudController::grenadeTypeFor("") == 0 && HudController::grenadeTypeFor("GrenadeLauncher") == 1 &&
          HudController::grenadeTypeFor("KamikazeMine") == 3 && HudController::targetTypeFor(-1, 255, 0) == 2 &&
          HudController::targetTypeFor(3, 0, 0) == 0 && HudController::targetTypeFor(3, 1, 0) == 1 && HudController::hudYaw(0.5) == -0.5 &&
          HudController::abilityIconId("Barrier") == "TnAbilityBarrier" && HudController::abilityIconId("TnAbilityHover") == "TnAbilityHover" &&
          HudController::abilityIconId("") == "None",
          "hud.conversions");
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
    {   // Escalation maps (Broken Hope 505 / Remnant 506: CompatibleGameTypes=SV only) are never offered for a versus mode.
        bool escInVersus = false, escInSv = false;
        for (const char* mode : {"TDM", "DM", "CTF", "KOTH", "DOM", "EXT", "CP"})
            for (const MapInfo* m : c.compatibleMaps(mode, false)) escInVersus |= m->mapId == 505 || m->mapId == 506;
        for (const MapInfo* m : c.compatibleMaps("SV", false)) escInSv |= m->mapId == 505 || m->mapId == 506;
        check(!escInVersus && escInSv, "catalog.escalation_maps_sv_only");
    }
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
    check(open2.empty() && ui2.openMovie().empty(), "ui.resume_closes_pause_movie");
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
    {   // Selected-character contract: the committed custom slot, resolved once (Soldier = Warpath / Brawl).
        GameFlow::SelectedCharacter s = rt.selectionFor("Soldier");
        check(s.specialty == "Soldier" && s.chassis[0] == "Tank3" && s.chassis[1] == "Tank2" && s.type == 0, "selection.soldier_chassis");
        check(!s.weapons.empty() && !s.vehicleWeapons.empty() && !s.melee.empty() && !s.abilities.empty(), "selection.soldier_loadout",
              std::to_string(s.weapons.size()) + " weapons");
        check(s.bodyAvailable[0] && s.bodyAvailable[1], "selection.soldier_bodies", s.robotGltf[0] + " | " + s.robotGltf[1]);
        check(s.primary[1].palette >= 5 && s.primary[1].palette <= 9 && s.primary[0].palette <= 4, "selection.palette_ranges");
        GameFlow::SelectedCharacter u = rt.selectionFor("NoSuchSlot");
        check(!u.bodyAvailable[0] && !u.bodyAvailable[1] && u.chassis[0].empty(), "selection.unknown_slot_no_body");
    }
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
    // Quit: TnQuitMessageBox (Yes / No), then TnPlayerController.QuitGame(0): a private match returns to the party
    // lobby; from the party lobby Quit returns to the front end [CONFIRMED script].
    f.call("Game.QuitToMainMenu");
    check(f.popup().open && f.popup().buttonString() == "$UIText.ButtonHints.Yes,MessageBox.OnA,$UIText.ButtonHints.No,MessageBox.OnB,,,,",
          "flow.quit_asks_confirmation", f.popup().buttonString());
    f.call("MessageBox.OnB");
    check(!f.popup().open && !f.wantsWorldUnload(), "flow.quit_no_cancels");
    f.call("Game.QuitToMainMenu");
    f.call("MessageBox.OnA");
    check(f.popup().open && f.popup().icon == 1, "flow.quit_yes_waits");
    f.tick(0.016f);
    check(f.wantsWorldUnload(), "flow.quit_unloads_world");
    f.worldUnloaded();
    check(runUntil(rt, [&] { return f.level() == LevelKind::PartyLobby && f.ui().state() == UIState::InLobby; }), "flow.match_quit_returns_to_party_lobby");
    f.call("Game.QuitToMainMenu");
    f.call("MessageBox.OnA");
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

// Customization camera per chassis (Chassis_To_Cam_ID*) and Matinee DrawScale tracks (vignette ships).
static void testSceneCamera() {
    FrontendScene sc;
    bool ok = sc.load(std::string(WFC_SOURCE_DIR) + "/data/frontend/scenes.json");
    check(ok, "scene.load");
    if (!ok) return;
    int camId[2] = {2, 2};   // Leader (Truck CustomizationCameraId 2) in both slots
    sc.cameraIdForSlot = [&](int slot) { return slot >= 0 && slot < 2 ? camId[slot] : -1; };
    sc.enterLevel("UI_PartyLobby_m");
    sc.trigger("FsCommand:initStreamingLvl");
    sc.tick(0.1);
    SceneView center = sc.view();
    sc.trigger("FsCommand:hideDecepticon");   // Autobot chassis menu open -> LEADER - Autobot plays
    sc.tick(1.0);
    SceneView a = sc.view();
    bool leader = false;
    for (const std::string& p : sc.playing()) leader = leader || p == "LEADER - Autobot";
    check(leader && a.pos[1] > center.pos[1] + 100 && a.fov < center.fov, "scene.customizeCamera.autobotLeader",
          "y " + std::to_string(center.pos[1]) + " -> " + std::to_string(a.pos[1]) + " fov " + std::to_string(a.fov));
    sc.trigger("FsCommand:unhideDecepticon");  // closed -> Reverse back to the centre pose
    sc.tick(1.0);
    SceneView b = sc.view();
    check(std::abs(b.pos[1] - center.pos[1]) < 1.0 && std::abs(b.fov - center.fov) < 0.01, "scene.customizeCamera.reverse");
    camId[1] = 0;                               // Scout in the Decepticon slot
    sc.trigger("FsCommand:hideAutobot");
    sc.tick(1.0);
    bool scout = false;
    for (const std::string& p : sc.playing()) scout = scout || p == "SCOUT - Decepticon";
    check(scout && sc.view().pos[1] < center.pos[1] - 100, "scene.customizeCamera.decepticonScout");
    // Title vignette: DrawScale keys (djDS01 0.08 before its first key at 236.47 s, booster 0.1 -> 1.0 by 249.42 s).
    FrontendScene t;
    t.load(std::string(WFC_SOURCE_DIR) + "/data/frontend/scenes.json");
    t.enterLevel("UI_FrontEnd_m");
    t.trigger("FsCommand:enterFrontEnd");
    t.tick(0.5);
    auto scaleOf = [&](const std::string& actor) {
        for (const auto& s2 : t.view().scales) if (s2.actor == actor) return s2.drawScale;
        return -1.0;
    };
    double dj0 = scaleOf("HmSkeletalMeshActor_6787");
    t.tick(250.0);
    double boost = scaleOf("Emitter_13686");
    check(std::abs(dj0 - 0.08) < 1e-3 && std::abs(boost - 1.0) < 1e-3, "scene.drawScale",
          "djDS01 " + std::to_string(dj0) + " DSbooster@250s " + std::to_string(boost));
}

int main() {
    std::string vs = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    Catalog c;
    bool ok = c.load(Catalog::defaultManifestRoot(), Catalog::defaultExtractedRoot(), vs + "/Maps");
    check(ok, "catalog.load");
    testUrl();
    testProfileBotMigration();
    testRecommendedBots();
    if (ok) testCatalog(c);
    if (ok) testHudObservers(c);
    testUIController();
    testSceneCamera();
    if (ok) testFlow();
    if (ok) testCatalogExtensibility(Catalog::defaultManifestRoot(), Catalog::defaultExtractedRoot());
    std::printf("frontend tests: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail;
}
