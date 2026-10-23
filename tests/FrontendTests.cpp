// Clean-room reconstruction — headless frontend / game-flow tests (no window, renderer or audio).
// Run: build/bin/wfc_frontend_tests.exe   (reads the AssetTools frontend manifests; WFC_FRONTEND_MANIFESTS overrides)
// Exit code = number of failed checks. Every check prints "PASS|FAIL <name> ..." for the Experimental harness.
#include "assets/Json.h"
#include "frontend/OpenPath.h"
#include "platform/Keyboard.h"
#include "frontend/Catalog.h"
#include "frontend/FrontendRuntime.h"
#include "frontend/GraphicsAutoDetect.h"
#include "frontend/FrontendScene.h"
#include "frontend/GameFlow.h"
#include "frontend/Hud.h"
#include "frontend/InputPrompts.h"
#include "frontend/FakeSession.h"
#include "frontend/FpsMeter.h"
#include "frontend/PackageCredits.h"
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
    // Bot AI (user 2026-10-13): no row, no profile value; an older profile's BotAI=Classic is dropped on save.
    {
        std::istringstream classic("[PCSettings]\nBotAI=Classic\nBotsAutobot=2\n");
        frontend::LocalProfile cl;
        cl.loadFrom(classic);
        cl.save();
        std::ifstream in("wfc_profile.ini");
        std::stringstream all;
        all << in.rdbuf();
        check(cl.bots.autobot == 2 && all.str().find("BotAI") == std::string::npos, "profile.bot_ai_line_dropped");
        std::remove("wfc_profile.ini");
    }
    // PC EXTENSION graphics options: off by default (the original look), persisted under [PCSettings].
    frontend::LocalProfile g;
    check(g.display.upscaling == 0 && !g.display.hdTextures && g.display.anisotropy == 4 && g.display.frameGeneration == 0 && !g.display.rayTracing,
          "profile.graphics_extensions_default_off");
    g.display.upscaling = 2;
    g.display.hdTextures = 2;
    g.display.anisotropy = 16;
    g.save();
    std::ifstream back("wfc_profile.ini");
    frontend::LocalProfile r;
    r.loadFrom(back);
    {   // Ray tracing: 0 Off (default) / 1 Faithful / 2 Realistic; HD textures: the user's pick is kept
        std::istringstream in("[PCSettings]\nRayTracing=2\nHDTexturesUserSet=1\nHDPackHintShown=1\n"), big("[PCSettings]\nRayTracing=9\n");
        frontend::LocalProfile a, b, d;
        a.loadFrom(in); b.loadFrom(big);
        check(a.display.rayTracing == 2 && b.display.rayTracing == 2 && d.display.rayTracing == 0 && a.display.hdTexturesUserSet &&
              a.display.hdPackHintShown && !d.display.hdTexturesUserSet && !d.display.hdPackHintShown && !d.display.hdTextures,
              "profile.ray_tracing_0_2_hd_user_set");
    }
    {   // HD textures 0 Off / 1 Faithful / 2 Enhanced: an old On (1) is Faithful; past the range clamps to Enhanced
        std::istringstream one("[PCSettings]\nHDTextures=1\n"), big("[PCSettings]\nHDTextures=7\n");
        frontend::LocalProfile a, b;
        a.loadFrom(one); b.loadFrom(big);
        check(a.display.hdTextures == 1 && b.display.hdTextures == 2, "profile.hd_textures_levels");
    }
    {   // Frame generation: 0 / 1 (any non-zero is On); default Off
        std::istringstream on("[PCSettings]\nFrameGeneration=7\n");
        frontend::LocalProfile a, d;
        a.loadFrom(on);
        check(a.display.frameGeneration == 1 && d.display.frameGeneration == 0, "profile.frame_generation_0_1");
    }
    {   // FSR 3.1 modes (4-6) persist; values past the range clamp to the last one
        std::istringstream f3("[PCSettings]\nUpscaling=5\n"), big9("[PCSettings]\nUpscaling=9\n");
        frontend::LocalProfile a, b;
        a.loadFrom(f3); b.loadFrom(big9);
        check(a.display.upscaling == 5 && b.display.upscaling == 6, "profile.upscaling_fsr31_range",
              std::to_string(a.display.upscaling) + "/" + std::to_string(b.display.upscaling));
    }
    check(r.display.upscaling == 2 && r.display.hdTextures == 2 && r.display.anisotropy == 16, "profile.graphics_extensions_roundtrip",
          std::to_string(r.display.upscaling) + "/" + std::to_string(r.display.hdTextures));
    back.close();
    std::remove("wfc_profile.ini");
    std::istringstream odd("[PCSettings]\nAnisotropy=12\n");
    frontend::LocalProfile o;
    o.loadFrom(odd);
    check(o.display.anisotropy == 8, "profile.anisotropy_snaps_to_4_8_16", std::to_string(o.display.anisotropy));
}

static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v) setenv(k, v, 1); else unsetenv(k);
#endif
}

static void testHdTexturesBoot() {
    {   // Pre-key profiles: an HD level that is on was the user's choice; Off (always written then) is not
        std::istringstream on("[PCSettings]\nHDTextures=1\n"), off("[PCSettings]\nHDTextures=0\n"),
            keyed("[PCSettings]\nHDTextures=2\nHDTexturesUserSet=0\n");
        frontend::LocalProfile a, b, c;
        a.loadFrom(on); b.loadFrom(off); c.loadFrom(keyed);
        check(a.display.hdTexturesUserSet && !b.display.hdTexturesUserSet && !c.display.hdTexturesUserSet, "profile.hd_user_set_pre_key_migration");
    }
    // The effective level falls to the next lower installed one, never up.
    check(frontend::hdTexturesEffective(2, true, true) == 2 && frontend::hdTexturesEffective(2, true, false) == 1 &&
          frontend::hdTexturesEffective(2, false, false) == 0 && frontend::hdTexturesEffective(1, false, true) == 0 &&
          frontend::hdTexturesEffective(0, true, true) == 0, "hd.effective_level_falls_back");
    // Boot with automated-run settings (WFC_AUTODETECT=0, or a script): no change to the stored values, no save, even
    // with the stored level's root missing (the session uses the effective level instead).
    const char* modes[][2] = {{"WFC_AUTODETECT", "0"}, {"WFC_FRONTEND_SCRIPT", "quit"}};
    for (auto& m : modes) {
        std::remove("wfc_profile.ini");
        setEnv(m[0], m[1]);
        FrontendRuntime rt;
        frontend::HardwareFacts f;
        f.gpuName = "Test GPU"; f.vramMB = 16384; f.hdTexturePack = false; f.hdEnhanced = false;
        rt.setHardwareFacts(f);
        rt.flow().profile().display.hdTextures = 2;
        const bool changed = rt.autoDetectGraphicsAtBoot();
        std::ifstream saved("wfc_profile.ini");
        check(!changed && rt.flow().profile().display.hdTextures == 2 && rt.hdTexturesEffectiveLevel() == 0 && !saved,
              (std::string("hd.boot_no_change_no_save_") + m[0]).c_str());
        setEnv(m[0], nullptr);
    }
    std::remove("wfc_profile.ini");
}

static void testVolumetricSetting() {
    {   // default On; persisted; the data store reads / writes it; its texts come from the rebuild's own table
        frontend::LocalProfile d;
        std::istringstream off("[PCSettings]\nVolumetricLighting=0\n");
        frontend::LocalProfile a;
        a.loadFrom(off);
        a.save();
        std::ifstream in("wfc_profile.ini");
        frontend::LocalProfile b;
        b.loadFrom(in);
        check(d.display.volumetricLighting && !a.display.volumetricLighting && !b.display.volumetricLighting,
              "profile.volumetric_default_on_persisted");
        in.close();   // (Windows cannot delete a file that is still open)
        std::remove("wfc_profile.ini");
    }
    FrontendRuntime rt;
    check(rt.init(), "volumetric.init");
    DataStores& ds = rt.dataStores();
    const bool readOn = ds.call("ReadValue", {"<PCSettings:VolumetricLighting>"}).s == "1";
    ds.call("WriteValue", {"<PCSettings:VolumetricLighting>", "0"});
    const bool wroteOff = !rt.flow().profile().display.volumetricLighting && ds.call("ReadValue", {"<PCSettings:VolumetricLighting>"}).s == "0";
    ds.call("WriteValue", {"<PCSettings:VolumetricLighting>", "1"});
    check(readOn && wroteOff && rt.flow().profile().display.volumetricLighting, "datastores.volumetric_read_write");
    check(rt.catalog().localizeKey("$WfcPC.Settings.VolumetricLighting") == "Volumetric Lighting" &&
          rt.catalog().localizeKey("$WfcPC.Settings.VolumetricRealisticOnly") == "Realistic only" &&
          rt.catalog().localizeKey("$WfcPC.Online.Rejoin") == "Rejoin", "catalog.wfcpc_strings_loaded");
    std::remove("wfc_profile.ini");
}

static void testCampaignStrings() {
    // <Strings:File.Section.Key>: the INT text in versus and campaign alike (the original's rename prompt / Claimed).
    // Hud_GFX's <Strings:TransGame.ContextualCommands.DownedSuicide> has no entry in the shipped TransGame.int (the text
    // is UIText [Hud] DownedSuicide, which the movie also names): it answers "" as the data has nothing there.
    FrontendRuntime rt;
    check(rt.init(), "campaign.init");
    DataStores& ds = rt.dataStores();
    const std::string m = "<Strings:UIText.Customization.NameTrans>";
    const bool versusText = ds.call("ReadValue", {m}).s == "Name Character" && !rt.flow().campaign() &&
                            ds.call("ReadValue", {"<Strings:UIText.Customization.NameDesc>"}).s == "Enter a name for this custom character build." &&
                            ds.call("ReadValue", {"<Strings:UIText.CharacterSelect.Unclaimed>"}).s == "Available" &&
                            ds.call("ReadValue", {"<Strings:Bad>"}).s.empty();
    rt.flow().setCampaignForTest(true);
    const std::string t = ds.call("ReadValue", {m}).s;
    const std::string u = ds.call("ReadValue", {"<Strings:UIText.CharacterSelect.Claimed>"}).s;
    const std::string d = ds.call("ReadValue", {"<Strings:TransGame.ContextualCommands.DownedSuicide>"}).s;
    rt.flow().setCampaignForTest(false);
    check(versusText && t == "Name Character" && u == "Selected" && d.empty() &&
          frontend::GameFlow::isCampaignTag("CP") && !frontend::GameFlow::isCampaignTag("TDM"), "datastores.strings_store", t + " / " + u);
}

static void testFakeSession() {
    // The session boundary's test double (S0): script entries fire by time or after a recorded call; calls are recorded.
    using E = SessionEvent::Type;
    FakeSession fs;
    FakeSession::Step opened; opened.afterCall = "host"; opened.delay = 1.0 / 60.0; opened.event.type = E::HostOpened;
    opened.event.joinCode = "WFC-7QK2-M9TD";
    FakeSession::Step lost; lost.at = 5.0; lost.event.type = E::HostLost;
    fs.add(opened); fs.add(lost);
    const bool quiet = fs.poll(0.0).empty();
    SessionHostSettings hs; hs.gameModeTag = "TDM";
    fs.host(hs);
    const bool notYet = fs.poll(0.0).empty();
    const std::vector<SessionEvent> a = fs.poll(1.0 / 60.0);
    const bool hostOpened = a.size() == 1 && a[0].type == E::HostOpened && a[0].joinCode == "WFC-7QK2-M9TD";
    const bool before = fs.poll(4.9).empty();
    const std::vector<SessionEvent> b = fs.poll(5.0);
    fs.requestStart();
    check(quiet && notYet && hostOpened && before && b.size() == 1 && b[0].type == E::HostLost && fs.scriptDone() &&
          fs.count("host") == 1 && fs.last("host")->hostSettings.gameModeTag == "TDM" && fs.count("requestStart") == 1 &&
          std::string(sessionEventName(E::Kicked)) == "Kicked" && std::string(sessionEventName(E::MemberVoice)) == "MemberVoice",
          "session.fake_script_and_recorder");
    SessionVoiceSettings vs; vs.mode = SessionVoiceMode::OpenMic; vs.audience = SessionVoiceAudience::AllRadio; vs.volume = 40;
    fs.setVoiceSettings(vs); fs.setPushToTalk(true); fs.setMuted(SessionMemberId{"WFC-AAAA-BBBB"}, true);
    check(fs.last("setVoiceSettings")->arg == "2,1,,40" && fs.last("setPushToTalk")->arg == "1" && fs.last("setMuted")->arg == "WFC-AAAA-BBBB,1",
          "session.fake_voice_calls");
    FrontendRuntime rt;
    check(rt.flow().session() == nullptr, "session.offline_by_default");
}

static void testFpsCounter() {
    {   // default Off; persisted as ShowFps
        frontend::LocalProfile d;
        std::istringstream on("[PCSettings]\nShowFps=1\n");
        frontend::LocalProfile a;
        a.loadFrom(on);
        a.save();
        std::ifstream in("wfc_profile.ini");
        frontend::LocalProfile b;
        b.loadFrom(in);
        check(!d.display.showFps && a.display.showFps && b.display.showFps, "profile.show_fps_default_off_persisted");
        in.close();
        std::remove("wfc_profile.ini");
    }
    // 4 Hz readout over a 0.5 s window: a steady 120 Hz reads 120; a drop to 60 Hz is half-way after one window, 60 after two.
    frontend::FpsMeter m;
    check(m.text() == "-- FPS", "fps.before_first_readout");
    int updates = 0;
    for (int i = 0; i <= 120; ++i) updates += m.frame(i / 120.0) ? 1 : 0;   // exact times: windows end on frames 30/60/90/120
    const bool steady = std::fabs(m.fps() - 120.0) < 1.0 && updates == 4;
    for (int i = 1; i <= 16; ++i) m.frame(1.0 + i / 60.0);
    const bool half = m.fps() > 80.0 && m.fps() < 100.0;
    for (int i = 17; i <= 32; ++i) m.frame(1.0 + i / 60.0);
    check(steady && half && std::fabs(m.fps() - 60.0) < 2.0 && m.text().find("FPS") != std::string::npos, "fps.window_4hz_0_5s",
          m.text());
}

static void testPackageCredits() {
    // The DLSS credit line: start-up movie or the credits movie, only in a DLSS package build; this test build is not one.
    check(!frontend::kDlssPackage, "credits.dlss_off_by_default");
    check(frontend::isCreditsMovie("FMV_credits") && frontend::isCreditsMovie("FMV_Credits_INT") && !frontend::isCreditsMovie("FMV_Intro"),
          "credits.movie_name");
    check(frontend::packageCreditShown(true, true, false, "") && frontend::packageCreditShown(true, false, true, "FMV_credits") &&
          !frontend::packageCreditShown(true, false, false, "FMV_credits") && !frontend::packageCreditShown(true, false, true, "FMV_Intro") &&
          !frontend::packageCreditShown(false, true, true, "FMV_credits"), "credits.dlss_line_moments");
    check(frontend::packageCreditShown(true, false, true, "Logo_Activision") && frontend::packageCreditShown(true, false, true, "Logo_HighMoon") &&
          !frontend::packageCreditShown(true, false, true, "FMV_intro") && !frontend::packageCreditShown(true, false, false, "Logo_Hasbro"),
          "credits.dlss_line_boot_logos");
    check(std::string(frontend::kDlssCreditText).rfind("NVIDIA DLSS: Copyright (c) NVIDIA Corporation.", 0) == 0, "credits.dlss_text");
}

static void testGraphicsAutoDetect() {
    // The shipped preset table (data/frontend/graphics_presets.json): first tier the PC meets.
    frontend::GraphicsPresetTable t;
    check(t.load(std::string(WFC_SOURCE_DIR) + "/data/frontend/graphics_presets.json"), "autodetect.table_loads");
    frontend::HardwareFacts big;
    big.gpuName = "NVIDIA GeForce RTX 4080/PCIe/SSE2"; big.gpuVendorId = 0x10DE; big.vramMB = 16376; big.cpuCores = 16;
    big.nativeW = 3840; big.nativeH = 2160; big.nativeHz = 144; big.hdTexturePack = true;
    frontend::GraphicsPreset a = t.pick(big);
    check(a.tier == "ultra" && a.width == 3840 && a.height == 2160 && a.fullscreen && a.frameLimit == 144 && a.anisotropy == 16 &&
          a.upscaling == 0 && a.textureQuality == 2, "autodetect.strong_pc_native_4k", a.tier + " " + a.why);
    big.hdTexturePack = false;
    {   // HD textures: never set by auto-detect; the one-time message only with the pack and >= 6 GB (user decision 2026-10-12)
        frontend::HardwareFacts six = big;
        six.hdTexturePack = true; six.vramMB = 6144;
        const bool yes = frontend::hdTexturesSuggested(six);
        six.vramMB = 5900;
        const bool lowVram = frontend::hdTexturesSuggested(six);
        six.vramMB = 16384; six.hdTexturePack = false;
        const bool none = frontend::hdTexturesSuggested(six);
        six.hdEnhanced = true;
        check(yes && !lowVram && !none && frontend::hdTexturesSuggested(six), "autodetect.hd_pack_hint_pack_and_6gb");
    }
    frontend::HardwareFacts mid = big;
    mid.gpuName = "NVIDIA GeForce RTX 3060 Ti"; mid.vramMB = 8192; mid.cpuCores = 8;
    frontend::GraphicsPreset b = t.pick(mid);
    check(b.tier == "high" && b.width == 3840 && b.upscaling == 1 && b.anisotropy == 16, "autodetect.mid_gpu_4k_fsr_quality", b.tier + " " + b.why);
    frontend::HardwareFacts low = big;
    low.gpuName = "NVIDIA GeForce GTX 1050 Ti"; low.vramMB = 4096; low.cpuCores = 4; low.nativeW = 1920; low.nativeH = 1080; low.nativeHz = 60;
    frontend::GraphicsPreset c = t.pick(low);
    check(c.tier == "medium" && c.width == 1920 && c.upscaling == 0 && c.anisotropy == 8 && c.frameLimit == 60,
          "autodetect.4gb_1080p_native", c.tier + " " + c.why);
    frontend::HardwareFacts igp = big;
    igp.gpuName = "Intel(R) UHD Graphics 770"; igp.gpuVendorId = 0x8086; igp.vramMB = 128; igp.cpuCores = 12; igp.nativeW = 2560; igp.nativeH = 1440;
    frontend::GraphicsPreset d = t.pick(igp);
    check(d.tier == "low" && d.height == 1080 && d.width == 1920 && d.upscaling == 2 && d.anisotropy == 4, "autodetect.integrated_low_1080_fsr",
          d.tier + " " + std::to_string(d.width) + "x" + std::to_string(d.height) + " " + d.why);
    check(t.isIntegrated("AMD Radeon(TM) Graphics") && !t.isIntegrated("AMD Radeon RX 7900 XTX"), "autodetect.integrated_keywords");
    // A presets file asking for an FSR 3.1 mode (4-6) still gets FSR 1 at most: FSR 3.1 is only an explicit user choice.
    frontend::GraphicsPresetTable f3;
    const bool f3ok = f3.loadFromString("{\"tiers\":[{\"name\":\"x\",\"nativePixelBudget\":100,\"upscalingOverBudget\":5}]}");
    const frontend::GraphicsPreset e = f3.pick(big);
    check(f3ok && e.upscaling == 3, "autodetect.never_picks_fsr31", std::to_string(e.upscaling));
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
    bool hudStageSize(double& w, double& h) const override { w = 1280; h = 720; return true; }
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

static void testInputPrompts(const Catalog& c) {
    frontend::InputPrompts ip;
    check(ip.load(c.extractedRoot()), "prompts.load_original_tables");
    // TranslateCommandsInString: ^COMMAND -> key -> ActionScript; console glyph tokens on the pad, PC key text otherwise
    check(ip.translate("Hold ^PICKUP for `p", true) == "Hold {x} for `p", "prompts.pad_token", ip.translate("Hold ^PICKUP for `p", true));
    check(ip.translate("Hold ^PICKUP for `p", false) == "Hold <font color='#FF9333'>E</font> for `p", "prompts.pc_key_text",
          ip.translate("Hold ^PICKUP for `p", false));
    check(ip.translate("Press ^USE_KILL_STREAK to activate `k", true) == "Press {dleft} to activate `k", "prompts.killstreak_pad");
    check(ip.translate("^THROW_GRENADE, ^JUMP.", false) == "<font color='#FF9333'>G</font>, <font color='#FF9333'>SPACE</font>.",
          "prompts.token_ends_at_punctuation", ip.translate("^THROW_GRENADE, ^JUMP.", false));
    check(ip.translate("^NOT_A_COMMAND x ^", false) == "^NOT_A_COMMAND x ^", "prompts.unbound_token_stays");
    bool allBound = true;
    for (const auto& [cmd, key] : frontend::InputPrompts::pcCommandKeys()) if (ip.pcKeyText(key).empty()) { allBound = false; check(false, "prompts.pc_key_exists", cmd + " -> " + key); }
    check(allBound, "prompts.every_pc_command_has_original_text");
    // the shipped strings per device: base = PC key text, _360 = console tokens
    {
        Catalog& cc = const_cast<Catalog&>(c);
        const std::string pc = cc.localizeKey("$UIText.Hud.Reload");
        cc.setConsoleStrings(true);
        const std::string console = cc.localizeKey("$UIText.Hud.Reload");
        cc.setConsoleStrings(false);
        check(pc == "<font color='#FF9333'>R</font> Reload" && console == "{x} Reload", "prompts.pc_and_console_strings", pc + " | " + console);
    }
    // last-used device: pad / keys switch at once, the mouse only after real travel
    frontend::InputDevice d;
    check(!d.pad(), "device.pc_default_keyboard");
    d.update(true, false, 0, 0, 0.016f);
    check(d.pad(), "device.pad_switches");
    d.update(false, false, 5, 3, 0.016f); d.update(false, false, 0, 0, 0.3f); d.update(false, false, 6, 0, 0.016f);
    check(d.pad(), "device.mouse_nudge_keeps_pad");
    for (int i = 0; i < 6; ++i) d.update(false, false, 10, 0, 0.016f);
    check(!d.pad(), "device.mouse_travel_switches");
    d.update(true, false, 0, 0, 0.016f); d.update(false, true, 0, 0, 0.016f);
    check(!d.pad(), "device.key_switches");
}

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
    // lock-on marker: stage pixels + UU, sent while up and once to remove it
    f.lockOnMarker = HudFrame::LockOnMarker{4, 0.5, 0.25, 12.5, true, ""};
    hud.setFrame(f); p.calls.clear(); hud.update(&p, c, true, true);
    check(p.has("_global.StartMarkerUpdate()") && p.has("_global.UpdateMarker(4,1250,640,180,1,LockOn,)") && p.has("_global.FinishMarkerUpdate()"),
          "hud.lockon_marker", p.calls.size() > 1 ? p.calls[1] : "");
    p.calls.clear(); hud.update(&p, c, true, true);
    bool quiet = p.count("_global.StartMarkerUpdate") == 0;
    f.lockOnMarker.reset();
    hud.setFrame(f); p.calls.clear(); hud.update(&p, c, true, true);
    check(quiet && p.has("_global.FinishMarkerUpdate()") && p.count("_global.UpdateMarker") == 0, "hud.lockon_marker_removed");
    p.calls.clear(); hud.update(&p, c, true, true);
    check(p.count("_global.StartMarkerUpdate") == 0, "hud.lockon_idle_silent");
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
    // 13 versus maps (frontend_maps manifest) + the 10 campaign chapters (TransLevels.ini, CAMPAIGN_FRONTEND_PLAN C3)
    check(c.maps().size() == 23 && c.mapById(96) && c.mapById(105) && c.mapById(96)->faction == 0 && c.mapById(101)->faction == 1,
          "catalog.map_providers", std::to_string(c.maps().size()));
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
    {   // DataStores::readCell (the PlayerList cell fast path) answers exactly as the generic ReadCollection[Bool]Value call.
        DataStores& ds = rt.dataStores();
        bool same = true;
        std::string detail;
        const char* markups[] = {"<CurrentGame:Players>", "<TnMenuItems:Maps>", "<NoSuchStore:X>"};
        const char* cols[] = {"PlayerName", "TeamID", "IsConnecting", "MapId", "MapFilename", "NoSuchColumn", ""};
        const char* rows[] = {"0", "1", "5", "", "NaN", "-1"};
        for (const char* m : markups)
            for (const char* c : cols)
                for (const char* r : rows)
                    for (int b = 0; b < 2; ++b) {
                        BridgeValue g = ds.call(b ? "ReadCollectionBoolValue" : "ReadCollectionValue", {m, c, r});
                        BridgeValue q = ds.readCell(m, c, r, b != 0);
                        if (g.kind != q.kind || g.b != q.b || g.s != q.s) { same = false; detail = std::string(m) + " " + c + " " + r; }
                    }
        check(same, "datastores.readcell_matches_call", detail);
    }
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
    // The Select Map selector's own first request (its list filling) arrives after the lobby opens; the autoplay
    // script's map (508) must still win, i.e. the script waits for the selector instead of being taken as its init.
    f.call("Online.SetSelectedMapID", {"501"});
    check(runUntil(rt, [&] { return f.lobby().mapId == 508; }, 60 * 5), "flow.autoplay_map_survives_selector_init",
          std::to_string(f.lobby().mapId));
    check(f.lobby().gameModeTag == "TDM" && f.lobby().mapSelectionMethod == 1, "flow.gamelobby_private_tdm");
    {   // Game Mode Options bot rows (user playtest 2026-10-13): TnGameSettings fields after the mode's own; Create Game's
        // write of an unchanged label is not an edit; a changed one applies.
        DataStores& ds = rt.dataStores();
        const std::string fields = ds.call("GetDataStoreFields", {"TnGameSettings"}).s;
        const bool rows = fields.find(",WfcPlayerLimit,WfcBotsAutobot,WfcBotsDecepticon,WfcBotDifficulty,WfcIconicCharacters") != std::string::npos &&
                          fields.find("WfcBots,") == std::string::npos && fields.rfind("WfcIconicCharacters") + 19 == fields.size();
        const bool header = ds.call("GetCollectionColumnHeader", {"<TnGameSettings:WfcBotDifficulty>", "0"}).s == "Bot Difficulty" &&
                            ds.call("GetCollectionRowCount", {"<TnGameSettings:WfcBotDifficulty>"}).n == 4 &&
                            ds.call("GetCollectionRowCount", {"<TnGameSettings:WfcBotsAutobot>"}).n == f.botMax("autobot") + 1 &&
                            ds.call("ReadValue", {"<TnGameSettings:WfcPlayerLimit>"}).s == "Original (5 v 5)";
        LocalProfile::Bots& b = f.profile().bots;
        const LocalProfile::Bots saved = b;
        b.editedSinceMap = false;
        ds.call("WriteValue", {"<TnGameSettings:WfcBotsAutobot>", std::to_string(b.autobot)});
        const bool unchanged = !b.editedSinceMap;
        ds.call("WriteValue", {"<TnGameSettings:WfcBotsAutobot>", "2"});
        ds.call("WriteValue", {"<TnGameSettings:WfcBotDifficulty>", "Expert"});
        const bool applied = b.autobot == 2 && b.editedSinceMap && b.difficulty == 3 &&
                             ds.call("ReadValue", {"<TnGameSettings:WfcBotsAutobot>"}).s == "2";
        // PC EXTENSION "Iconic Characters" (OnlyAllowIconicCharacters; RE: no shipped versus class sets it): Off by default
        // (GRI _bIconicMode 0, the profile has no key); On -> _bIconicMode 1 and the URLs carry ?IconicMode=1
        const bool iconicOff = ds.call("ReadValue", {"<TnGameSettings:WfcIconicCharacters>"}).s == "Off" &&
                               ds.call("ReadValue", {"<CurrentGame:_bIconicMode>"}).s == "0" && !f.iconicMode();
        ds.call("WriteValue", {"<TnGameSettings:WfcIconicCharacters>", "On"});
        const bool iconicOn = b.iconic && f.iconicMode() && ds.call("ReadValue", {"<CurrentGame:_bIconicMode>"}).s == "1" &&
                              f.lobbyUrlForTest().find("?IconicMode=1") != std::string::npos;
        b = saved;
        f.profile().save();
        check(iconicOff && iconicOn && !f.iconicMode(), "lobby.iconic_characters_row", std::to_string(iconicOff) + std::to_string(iconicOn));
        check(rows && header && unchanged && applied, "lobby.mode_options_bot_rows", fields);
    }
    {   // An Escalation-only map (CompatibleGameTypes=SV) requested for TDM is refused; a TDM map is accepted.
        const int before = f.lobby().mapId;
        f.call("Online.SetSelectedMapID", {"505"});
        const bool refused = f.lobby().mapId == before;
        f.call("Online.SetSelectedMapID", {"508"});
        check(refused && f.lobby().mapId == 508, "flow.incompatible_map_refused", std::to_string(before) + " -> " + std::to_string(f.lobby().mapId));
    }
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

static void testCampaignObjectives() {
    // E1: the bank (test-only until AssetTools' map export carries TnWorldInfo._ObjectiveBank), the current objective,
    // the HUD's AddObjective on change / on a fresh movie, the pause menu's ActiveObjectives, versus silent.
    FrontendRuntime rt;
    check(rt.init(), "objectives.init");
    GameFlow& f = rt.flow();
    CampaignObjectives& o = f.objectives();
    o.setBank({"D1_ORB_OBJ01", "D1_ORB_OBJ02", "D1_ORB_OBJ03"});
    RecordingPresenter p;
    HudController hud;
    HudFrame fr; fr.valid = true;
    hud.setFrame(fr);
    auto frame = [&]() { hud.setObjective(f.campaign() ? o.activeText() : std::string()); hud.update(&p, rt.catalog(), true, true); };
    auto adds = [&]() { int n = 0; for (const auto& c : p.calls) n += c.rfind("_global.AddObjective(", 0) == 0; return n; };
    o.setCurrent("D1_ORB_OBJ01", rt.catalog());
    frame();
    const bool versusSilent = adds() == 0 && rt.dataStores().call("ReadValue", {"<CurrentGame:ActiveObjectives>"}).s.empty();
    f.setCampaignForTest(true);
    p.calls.clear(); frame();
    const bool first = adds() == 1 && p.has("_global.AddObjective(Escape the crash site)");
    p.calls.clear(); frame(); frame();
    const bool same = adds() == 0;
    o.setCurrent("D1_ORB_OBJ02", rt.catalog());
    p.calls.clear(); frame();
    const bool next = adds() == 1 && p.has("_global.AddObjective(Secure the station)") &&
                      rt.dataStores().call("ReadValue", {"<CurrentGame:ActiveObjectives>"}).s == "Secure the station" &&
                      o.completedTexts(rt.catalog()) == std::vector<std::string>{"Escape the crash site"};
    const bool unknown = !o.setCurrent("NO_SUCH_KEY", rt.catalog()) && o.current() == 1;
    hud.update(&p, rt.catalog(), false, false);   // the HUD closes and reopens (map load): the current one again
    p.calls.clear(); frame();
    const bool reopened = adds() == 1 && p.has("_global.AddObjective(Secure the station)");
    o.setCurrent("", rt.catalog());
    p.calls.clear(); frame();
    const bool none = adds() == 0 && rt.dataStores().call("ReadValue", {"<CurrentGame:ActiveObjectives>"}).s.empty();
    f.setCampaignForTest(false);
    check(versusSilent && first && same && next && unknown && reopened && none, "hud.campaign_objective",
          std::to_string(versusSilent) + std::to_string(first) + std::to_string(same) + std::to_string(next) + std::to_string(unknown) +
          std::to_string(reopened) + std::to_string(none));
}

static void testCampaignTutorials() {
    // E5: once per profile (C4), the HUD call with the localized text, versus silent, the record survives a reload.
    std::remove("wfc_profile.ini");
    {
        FrontendRuntime rt;
        check(rt.init(), "tutorials.init");
        RecordingPresenter p;
        HudFrame fr; fr.valid = true;
        rt.hud().setFrame(fr);
        auto frame = [&]() { rt.hud().update(&p, rt.catalog(), true, true); };
        auto count = [&]() { int n = 0; for (const auto& c : p.calls) n += c.rfind("_global.NotifyTutorialMessage(", 0) == 0; return n; };
        frame(); p.calls.clear();
        const bool versus = !rt.showTutorialMessage("TMT_SeeHealthTutorial");
        frame();
        const bool versusSilent = versus && count() == 0 && rt.flow().profile().tutorialsShown.empty();
        rt.flow().setCampaignForTest(true);
        const bool first = rt.showTutorialMessage("TMT_SeeHealthTutorial");
        frame();
        const bool sent = count() == 1 && p.has("_global.NotifyTutorialMessage(Health)");
        p.calls.clear();
        const bool again = !rt.showTutorialMessage("TMT_SeeHealthTutorial");
        const bool unknown = !rt.showTutorialMessage("TMT_NoSuchTutorial");
        const bool second = rt.showTutorialMessage("TMT_SeePickupTutorial");
        frame();
        check(versusSilent && first && sent && again && unknown && second && count() == 1 && p.has("_global.NotifyTutorialMessage(Pickups)"),
              "hud.campaign_tutorial_once",
              std::to_string(versusSilent) + std::to_string(first) + std::to_string(sent) + std::to_string(again) + std::to_string(unknown) + std::to_string(second));
    }
    {
        std::ifstream in("wfc_profile.ini");
        frontend::LocalProfile q;
        q.loadFrom(in);
        in.close();
        check(q.tutorialsShown == std::vector<std::string>{"TMT_SeeHealthTutorial", "TMT_SeePickupTutorial"}, "profile.tutorials_shown_persisted");
        FrontendRuntime rt2;
        rt2.init();
        rt2.flow().setCampaignForTest(true);
        check(!rt2.showTutorialMessage("TMT_SeeHealthTutorial") && rt2.showTutorialMessage("TMT_ShootingTutorial"), "hud.campaign_tutorial_next_session");
    }
    std::remove("wfc_profile.ini");
}

static void testCampaignCinematicHud() {
    // E7: cinematic mode hides the HUD (UI event 12) and empties the prompt stack; leaving it shows both again (13).
    // Versus: no change.
    std::remove("wfc_profile.ini");
    FrontendRuntime rt;
    check(rt.init(), "cinematic.init");
    platform::InputFrame none;
    rt.setCinematicMode(true);
    const bool versusUntouched = !rt.flow().ui().hudHiddenByEvent() && !rt.cinematicHudHidden();
    rt.flow().setCampaignForTest(true);
    rt.setCinematicMode(true);
    const bool hidden = rt.flow().ui().hudHiddenByEvent() && rt.cinematicHudHidden();
    rt.setCinematicMode(false);
    const bool pendingUntilUpdate = rt.flow().ui().hudHiddenByEvent();
    rt.update(none, 1.0f / 60.0f);
    const bool shown = !rt.flow().ui().hudHiddenByEvent() && !rt.cinematicHudHidden();
    rt.flow().setCampaignForTest(false);
    // the prompt stack on the movie: removed while suppressed, re-added after
    RecordingPresenter p;
    HudController hud;
    HudFrame fr; fr.valid = true; fr.contextualPrompts = std::vector<std::string>{"Use Turret", "Hack Terminal"};
    hud.setFrame(fr); hud.update(&p, rt.catalog(), true, true);
    p.calls.clear();
    hud.setPromptsSuppressed(true); hud.setFrame(fr); hud.update(&p, rt.catalog(), true, true);
    const bool removed = p.has("_global.NotifyContextualCommand(0,1,Use Turret)") && p.has("_global.NotifyContextualCommand(0,1,Hack Terminal)");
    p.calls.clear();
    hud.setFrame(fr); hud.update(&p, rt.catalog(), true, true);
    const bool quiet = p.calls.empty();
    hud.setPromptsSuppressed(false); hud.setFrame(fr); hud.update(&p, rt.catalog(), true, true);
    const bool readded = p.has("_global.NotifyContextualCommand(0,0,Use Turret)") && p.has("_global.NotifyContextualCommand(0,0,Hack Terminal)");
    check(versusUntouched && hidden && pendingUntilUpdate && shown && removed && quiet && readded, "ui.cinematic_hide_show",
          std::to_string(versusUntouched) + std::to_string(hidden) + std::to_string(pendingUntilUpdate) + std::to_string(shown) +
          std::to_string(removed) + std::to_string(quiet) + std::to_string(readded));
    std::remove("wfc_profile.ini");
}

static void testOnlineBoundary() {
    // Group A (ONLINE_MP_FRONTEND_TESTS.md S24 + the flag): a session set but quiet keeps the offline answers; events are
    // polled once per frontend update on frontend time.
    std::remove("wfc_profile.ini");
    FrontendRuntime rt;
    check(rt.init(), "online.init");
    GameFlow& f = rt.flow();
    const bool offline = !f.online() && f.call("Online.IsHost").b && f.sessionRole() == GameFlow::SessionRole::Offline;
    auto fs = std::make_unique<FakeSession>();
    FakeSession* fake = fs.get();
    FakeSession::Step s; s.at = 0.5; s.event.type = SessionEvent::Type::LobbyState;
    fake->add(s);
    f.setSession(std::move(fs));
    platform::InputFrame none;
    for (int i = 0; i < 20; ++i) rt.update(none, 1.0f / 60.0f);
    const bool notYet = !fake->scriptDone();
    for (int i = 0; i < 20; ++i) rt.update(none, 1.0f / 60.0f);
    check(offline && f.online() && f.call("Online.IsHost").b && notYet && fake->scriptDone(), "online.boundary_polled_on_frontend_time");
    std::remove("wfc_profile.ini");
}

// Online phase 1 test rig: a runtime in the party lobby with a FakeSession (ONLINE_MP_FRONTEND_TESTS.md section 3).
struct OnlineRig {
    std::unique_ptr<FrontendRuntime> rt = std::make_unique<FrontendRuntime>();
    FakeSession* fake = nullptr;
    bool ok = false;
    explicit OnlineRig(std::vector<FakeSession::Step> script = {}) {
        setEnv("WFC_FRONTEND_AUTOPLAY", nullptr);
        std::remove("wfc_profile.ini");
        if (!rt->init()) return;
        rt->flow().profile().onlineFirstRunShown = true;   // the one-time prompt is tested on its own (FR1-FR4)
        auto fs = std::make_unique<FakeSession>(std::move(script));
        fake = fs.get();
        rt->flow().setSession(std::move(fs));
        GameFlow& f = rt->flow();
        if (!runUntil(*rt, [&] { return f.frontEndStarted(); })) return;
        f.call("Online.OpenPartyLobby", {"GTS_TeamGame"});
        ok = runUntil(*rt, [&] { return f.level() == LevelKind::PartyLobby && !f.loading().active; });
    }
    GameFlow& f() { return rt->flow(); }
    std::string t(const char* k) { return rt->catalog().localizeKey(k); }
    bool popupIs(const char* title, const char* msg) { return f().popup().open && f().popup().title == t(title) && f().popup().message == t(msg); }
    void frames(int n) { platform::InputFrame none; for (int i = 0; i < n; ++i) rt->update(none, 1.0f / 60.0f); }
    ~OnlineRig() { std::remove("wfc_profile.ini"); }
};

static SessionEvent ev(SessionEvent::Type t) { SessionEvent e; e.type = t; return e; }
static FakeSession::Step after(const char* call, SessionEvent e, double delay = 1.0 / 60.0) {
    FakeSession::Step s; s.afterCall = call; s.delay = delay; s.event = std::move(e); return s;
}

static void testOnlineHostJoin() {
    using T = SessionEvent::Type;
    {   // S1 host succeeds
        SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD"; o.address = "192.168.1.20:7777";
        OnlineRig r({after("host", o, 0.5)});
        r.f().call("Online.EditGameMode", {"TDM"});
        r.f().call("Online.PlayPrivateGame", {"TDM"});
        const bool hosting = r.popupIs("$UIText.MessagePrompts.HostGameTitle", "$UIText.MessagePrompts.HostingMessage") && r.fake->count("host") == 1;
        const bool inLobby = runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; });
        r.frames(2);
        const int firstPublishes = r.fake->count("publishLobby");
        const int pick = r.f().lobby().mapId == 508 ? 502 : 508;   // a different map than the lobby opened with: one republish
        r.f().call("Online.SetSelectedMapID", {std::to_string(pick)});
        r.frames(2);
        check(r.ok && hosting && inLobby && firstPublishes == 1 && r.f().sessionRole() == GameFlow::SessionRole::Host && r.f().call("Online.IsHost").b &&
              r.f().joinCode() == "WFC-7QK2-M9TD" && r.f().joinAddress() == "192.168.1.20:7777" && !r.f().popup().open &&
              r.rt->dataStores().call("ReadBoolValue", {"<CurrentGame:CanHostRequestGameStart>"}).b && r.fake->count("publishLobby") == 2 &&
              r.fake->last("publishLobby")->lobby.gameModeTag == "TDM" && r.fake->last("publishLobby")->lobby.mapId == pick, "online.S1_host",
              std::to_string(r.ok) + std::to_string(hosting) + std::to_string(inLobby) + " role " + GameFlow::sessionRoleName(r.f().sessionRole()) +
              " code " + r.f().joinCode() + " popup " + std::to_string(r.f().popup().open) + " canStart " +
              std::to_string(r.rt->dataStores().call("ReadBoolValue", {"<CurrentGame:CanHostRequestGameStart>"}).b) + " publishes " +
              std::to_string(r.fake->count("publishLobby")));
    }
    {   // S2 host fails
        SessionEvent f = ev(T::HostFailed); f.hostFailure = SessionHostFailure::NoPath;
        OnlineRig r({after("host", f)});
        r.f().call("Online.EditGameMode", {"TDM"});
        r.f().call("Online.PlayPrivateGame", {"TDM"});
        r.frames(5);
        check(r.ok && r.popupIs("$UIText.MessagePrompts.StrictNATTitle", "$WfcPC.Online.HostNoPath") && r.f().level() == LevelKind::PartyLobby &&
              r.fake->count("publishLobby") == 0 && r.f().sessionRole() == GameFlow::SessionRole::Offline, "online.S2_host_fails");
    }
    SessionLobby hostLobby;
    hostLobby.gameModeTag = "TDM"; hostLobby.mapId = 508; hostLobby.mapSelectionMethod = 1;
    {   // S3 join succeeds; S9 the member cannot drive the lobby
        SessionEvent j = ev(T::Joined); j.lobby = hostLobby;
        OnlineRig r({after("join", ev(T::JoinProgress)), after("join", j, 0.5)});
        const bool invalid = !r.f().call("Online.JoinGame", {"not a code"}).b &&
                             r.popupIs("$UIText.MessagePrompts.GenericErrorTitle", "$WfcPC.Online.JoinInvalid") && r.fake->count("join") == 0;
        r.f().call("MessageBox.OnA");
        r.f().call("Online.JoinGame", {" wfc-7qk2-m9td "});
        const bool joining = r.popupIs("$UIText.MessagePrompts.JoinFriendGameTitle", "$UIText.MessagePrompts.JoiningMessage") &&
                             r.fake->last("join") && r.fake->last("join")->arg == "wfc-7qk2-m9td";
        const bool inLobby = runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; });
        r.frames(2);
        DataStores& ds = r.rt->dataStores();
        const bool member = r.f().sessionRole() == GameFlow::SessionRole::Member && !r.f().call("Online.IsHost").b && r.f().lobby().mapId == 508 &&
                            !ds.call("ReadBoolValue", {"<CurrentGame:CanHostRequestGameStart>"}).b &&
                            ds.call("ReadValue", {"<CurrentGame:LobbyStatus>"}).s == r.rt->catalog().localize("TransGame", "TnGameReplicationInfoGameLobby", "WaitingForHostToStartStatus");
        check(r.ok && invalid && joining && inLobby && member && !r.f().popup().open, "online.S3_join");
        // S9
        r.f().call("Online.SetSelectedMapID", {"502"});
        r.f().call("Online.BeginLobbyExitCountdown");
        ds.call("WriteValue", {"<TnGameSettings:TimeLimit>", "5 minutes"});
        r.frames(3);
        check(r.f().lobby().mapId == 508 && !r.f().lobby().countingDown && r.fake->count("requestStart") == 0 && r.fake->count("publishLobby") == 0,
              "online.S9_member_cannot_drive_lobby");
        // the host changes the map: the member follows
        SessionEvent ls = ev(T::LobbyState); ls.lobby = hostLobby; ls.lobby.mapId = 502;
        r.fake->inject(ls);
        r.frames(3);
        check(r.f().lobby().mapId == 502, "online.member_follows_host_lobby");
    }
    struct Fail { SessionJoinFailure why; const char* title; const char* msg; const char* name; };
    const Fail fails[] = {
        {SessionJoinFailure::Full, "$UIText.MessagePrompts.JoinGameInviteFailedTitle", "$UIText.MessagePrompts.JoinGameInviteFailedMessage", "online.S4_join_full"},
        {SessionJoinFailure::Version, "$UIText.MessagePrompts.JoinGameInviteFailedTitle", "$Engine.Errors.ClientOutdated", "online.S6_join_version"},
        {SessionJoinFailure::MapMissing, "$UIText.MessagePrompts.LobbyInvalidMapTitle", "$UIText.MessagePrompts.LobbyInvalidMapMessage", "online.S7_join_map_missing"},
    };
    for (const Fail& x : fails) {   // S4, S6, S7
        SessionEvent e = ev(T::JoinFailed); e.joinFailure = x.why;
        OnlineRig r({after("join", e)});
        r.f().call("Online.JoinGame", {"WFC-AAAA-BBBB"});
        r.frames(5);
        check(r.ok && r.popupIs(x.title, x.msg) && !r.rt->catalog().localizeKey(x.msg).empty() && r.f().level() == LevelKind::PartyLobby &&
              r.f().sessionRole() == GameFlow::SessionRole::Offline, x.name);
    }
    {   // S5 timeout: the session layer's JoinFailed{Timeout} at the original 50 s
        SessionEvent e = ev(T::JoinFailed); e.joinFailure = SessionJoinFailure::Timeout;
        OnlineRig r({after("join", e, 50.0)});
        r.f().call("Online.JoinGame", {"192.168.1.20:7777"});
        r.frames(2990);
        const bool still = r.popupIs("$UIText.MessagePrompts.JoinFriendGameTitle", "$UIText.MessagePrompts.JoiningMessage");
        r.frames(20);
        check(r.ok && still && r.popupIs("$UIText.MessagePrompts.JoinGameInviteFailedTitle", "$UIText.MessagePrompts.JoinGameInviteFailedMessage") &&
              r.fake->count("cancelJoin") == 0, "online.S5_join_timeout");
    }
    {   // the frontend backstop: a session that never answers fails the join at 55 s and cancels it
        OnlineRig r;
        r.f().call("Online.JoinGame", {"WFC-7QK2-M9TD-4H"});
        r.frames(3280);
        const bool still = r.popupIs("$UIText.MessagePrompts.JoinFriendGameTitle", "$UIText.MessagePrompts.JoiningMessage");
        r.frames(40);
        check(r.ok && still && r.popupIs("$UIText.MessagePrompts.JoinGameInviteFailedTitle", "$UIText.MessagePrompts.JoinGameInviteFailedMessage") &&
              r.fake->count("cancelJoin") == 1, "online.join_backstop_55s");
    }
    {   // code shapes: Systems' codes and relay codes pass to the session; its decode failure shows the entry text
        SessionEvent e = ev(T::JoinFailed); e.joinFailure = SessionJoinFailure::Invalid;
        OnlineRig r({after("join", e)});
        const bool shapes = r.f().call("Online.JoinGame", {"WFC-R-ABCD-EFGH"}).b;
        r.frames(5);
        const bool decodeFail = r.popupIs("$UIText.MessagePrompts.GenericErrorTitle", "$WfcPC.Online.JoinInvalid");
        r.f().call("MessageBox.OnA");
        // Systems' formats (decodeTarget parses): a LAN code (2 groups of 5), dashes optional, an internet code (4 groups),
        // the lobby service's 6 letters, a plain IPv4
        for (const char* ok : {"7QK2M-9TDAB", "7qk2m9tdab", "7QK2M-9TDAB-XXXXX-YYYYY", "ABCDEF", "192.168.1.20"}) {
            r.f().call("Online.JoinGame", {ok});
            r.f().call("MessageBox.OnA");
            r.frames(2);
        }
        const bool rejects = !r.f().call("Online.JoinGame", {"WFC--AB"}).b && !r.f().call("Online.JoinGame", {"WFC-"}).b &&
                             !r.f().call("Online.JoinGame", {"7QK2M-9TDAB-XXXXX-YYYYY-ZZZZZ"}).b &&
                             !r.f().call("Online.JoinGame", {"host:0"}).b && !r.f().call("Online.JoinGame", {"host:70000"}).b;
        check(r.ok && shapes && decodeFail && rejects && r.fake->count("join") == 6, "online.join_code_shapes", std::to_string(r.fake->count("join")));
    }
    {   // cancel while joining
        OnlineRig r;
        r.f().call("Online.JoinGame", {"WFC-AAAA-BBBB"});
        r.f().call("MessageBox.OnB");
        r.frames(3);
        check(r.ok && r.fake->count("cancelJoin") == 1 && !r.f().popup().open && r.f().sessionRole() == GameFlow::SessionRole::Offline, "online.join_cancel");
    }
    {   // S8 join during a match: the member waits in the lobby with "Match in progress"
        SessionEvent m = ev(T::MatchInProgress); m.lobby = hostLobby; m.lobby.matchRunning = true;
        OnlineRig r({after("join", m)});
        r.f().call("Online.JoinGame", {"WFC-AAAA-BBBB"});
        runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; });
        r.frames(2);
        DataStores& ds = r.rt->dataStores();
        const bool waiting = ds.call("ReadValue", {"<CurrentGame:LobbyStatus>"}).s == "Match in progress" &&
                             !ds.call("ReadBoolValue", {"<CurrentGame:CanHostRequestGameStart>"}).b;
        SessionEvent back = ev(T::LobbyState); back.lobby = hostLobby;
        r.fake->inject(back);
        r.frames(3);
        check(r.ok && waiting && ds.call("ReadValue", {"<CurrentGame:LobbyStatus>"}).s != "Match in progress", "online.S8_join_during_match");
    }
}

static SessionEvent memberEv(const char* fp, const char* name, bool hasChar, int teamRequest = -1, SessionPlatform pl = SessionPlatform::PC) {
    SessionEvent e = ev(SessionEvent::Type::MemberJoined);
    e.member.fingerprint = fp;
    e.info.id = e.member; e.info.displayName = name; e.info.hasCharacter = hasChar; e.info.teamRequest = teamRequest; e.info.platform = pl;
    if (hasChar) e.info.characterSlot = "Soldier";
    return e;
}

// A host in its game lobby (TDM, map 508 picked), 4 + 4 bots set.
static bool hostLobby(OnlineRig& r) {
    r.f().profile().bots.autobot = 4; r.f().profile().bots.decepticon = 4; r.f().profile().bots.extended = false;
    r.f().call("Online.EditGameMode", {"TDM"});
    r.f().call("Online.PlayPrivateGame", {"TDM"});
    if (!runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; })) return false;
    r.frames(2);
    if (r.f().lobby().mapId != 508) r.f().call("Online.SetSelectedMapID", {"508"});
    r.frames(2);
    return true;
}

static void testOnlineLobbyGate() {
    using T = SessionEvent::Type;
    SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
    OnlineRig r({after("host", o)});
    const bool host = r.ok && hostLobby(r);
    DataStores& ds = r.rt->dataStores();
    auto status = [&]() { return ds.call("ReadValue", {"<CurrentGame:LobbyStatus>"}).s; };
    auto canStart = [&]() { return ds.call("ReadBoolValue", {"<CurrentGame:CanHostRequestGameStart>"}).b; };
    auto st = [&](const char* k) { return r.rt->catalog().localize("TransGame", "TnGameReplicationInfoGameLobby", k); };
    // S10: a joiner without a character holds the start; with one, the host may start
    r.fake->inject(memberEv("WFC-AAAA-AAAA-1", "Alpha", false));
    r.frames(2);
    const bool waitChar = status() == st("WaitingForCharacterChoiceStatus") && !canStart();
    r.fake->inject(memberEv("WFC-AAAA-AAAA-1", "Alpha", true));
    r.frames(2);
    const bool ready = status() == st("WaitingForHostToStartStatus") && canStart();
    // unbalanced teams hold the start with Team Balancing "Manual" (Autobalanced balances them at the countdown)
    r.f().call("Online.EditGameMode", {"TDM"});
    ds.call("WriteValue", {"<TnGameSettings:AutobalanceTeams>", "Manual"});
    r.fake->inject(memberEv("WFC-BBBB-BBBB-2", "Bravo", true, 1));
    r.fake->inject(memberEv("WFC-CCCC-CCCC-3", "Charlie", true, 1));
    r.frames(2);
    r.fake->inject(memberEv("WFC-BBBB-BBBB-2", "Bravo", true, 1));   // (updates: the team requests apply)
    r.fake->inject(memberEv("WFC-CCCC-CCCC-3", "Charlie", true, 1));
    r.fake->inject(memberEv("WFC-AAAA-AAAA-1", "Alpha", true, 1));
    r.frames(2);
    ds.poll();   // (no presenter polls headless: a new frame for the data-store cache)
    const bool unbalanced = status() == st("WaitingForBalancedTeamsStatus") && !canStart();
    r.fake->inject(memberEv("WFC-CCCC-CCCC-3", "Charlie", true, 0));
    r.frames(2);
    const bool balanced = status() == st("WaitingForHostToStartStatus") && canStart();
    check(host && waitChar && ready && unbalanced && balanced, "online.S10_lobby_gate",
          std::to_string(host) + std::to_string(waitChar) + std::to_string(ready) + std::to_string(unbalanced) + std::to_string(balanced) + " " + status());
    // S11: humans take bot slots (team 0: host + Charlie, team 1: Alpha + Bravo; 5 a side -> 3 bots each)
    const SessionLobby* pub = r.fake->last("publishLobby") ? &r.fake->last("publishLobby")->lobby : nullptr;
    check(r.f().onlineBotsFor(0) == 3 && r.f().onlineBotsFor(1) == 3 && r.f().profile().bots.autobot == 4 && pub && pub->slots.size() == 4 &&
          pub->members.size() == 4, "online.S11_humans_take_bot_slots",
          std::to_string(r.f().onlineBotsFor(0)) + "/" + std::to_string(r.f().onlineBotsFor(1)) + " slots " + std::to_string(pub ? pub->slots.size() : 0));
    // S13: platform tags in the player list (the local player first)
    const std::string row0 = ds.call("ReadCollectionValue", {"<CurrentGame:Players>", "PlayerName", "0"}).s;
    const std::string row1 = ds.call("ReadCollectionValue", {"<CurrentGame:Players>", "PlayerName", "1"}).s;
    // S12: a browser peer caps Player Limit to 5 v 5
    r.f().profile().bots.extended = true;
    r.fake->inject(memberEv("WFC-WWWW-WWWW-4", "Webby", true, -1, SessionPlatform::Web));
    r.frames(2);
    ds.poll();
    const std::string rowWeb = ds.call("ReadCollectionValue", {"<CurrentGame:Players>", "PlayerName", "4"}).s;
    check(!r.f().profile().bots.extended, "online.S12_browser_cap");
    check(row0.size() > 4 && row0.substr(row0.size() - 4) == "  PC" && row1 == "Alpha  PC" && rowWeb == "Webby  Web" &&
          ds.call("GetCollectionRowCount", {"<CurrentGame:Players>"}).n == 5, "online.S13_platform_tags", row0 + " | " + row1 + " | " + rowWeb);
    // a member leaves: its row and slot go
    SessionEvent left = ev(T::MemberLeft); left.member.fingerprint = "WFC-WWWW-WWWW-4";
    r.fake->inject(left);
    r.frames(2);
    ds.poll();
    check(ds.call("GetCollectionRowCount", {"<CurrentGame:Players>"}).n == 4 && r.f().onlineMembers().size() == 3, "online.member_left_row_gone");
}

static void testOnlineMemberSlots() {
    // A member finds itself in the host's slots (its team) and lists the other humans.
    using T = SessionEvent::Type;
    SessionLobby l; l.gameModeTag = "TDM"; l.mapId = 508; l.mapSelectionMethod = 1;
    SessionMemberInfo hostInfo; hostInfo.displayName = "Hosty"; hostInfo.hasCharacter = true;
    SessionMemberInfo me; me.id.fingerprint = "WFC-SELF-SELF-9"; me.displayName = "Me"; me.hasCharacter = true;
    l.members = {hostInfo, me};
    SessionSlot hs; hs.kind = SessionSlot::Kind::Human; hs.team = 0;
    SessionSlot ms; ms.kind = SessionSlot::Kind::Human; ms.member = me.id; ms.team = 1;
    l.slots = {hs, ms};
    SessionEvent j = ev(T::Joined); j.lobby = l; j.member = me.id;
    OnlineRig r({after("join", j)});
    r.f().call("Online.JoinGame", {"WFC-7QK2-M9TD-4H"});
    runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; });
    r.frames(3);
    const std::string row1 = r.rt->dataStores().call("ReadCollectionValue", {"<CurrentGame:Players>", "PlayerName", "1"}).s;
    check(r.ok && r.f().lobby().localTeam == 1 && r.f().onlineMembers().size() == 1 && row1 == "Hosty  PC", "online.member_slots_from_host", row1);
}

static void testHudJamBuffs() {
    // ApplyBuff / UnapplyBuff for the jam / disrupt buffs only, on change; all active again on a fresh movie.
    RecordingPresenter p;
    HudController hud;
    frontend::Catalog cat;
    HudFrame f; f.valid = true;
    f.jamBuffs = std::vector<HudFrame::Buff>{};
    hud.setFrame(f); hud.update(&p, cat, true, true);
    p.calls.clear();
    f.jamBuffs = std::vector<HudFrame::Buff>{{"TnBuffAbilityJammedKillstreak", 15}, {"TnBuffSeeEnemies", 30}};
    hud.setFrame(f); hud.update(&p, cat, true, true);
    const bool applied = p.has("_global.ApplyBuff(TnBuffAbilityJammedKillstreak,0,15)") && p.calls.size() == 1;
    p.calls.clear();
    f.jamBuffs->front().remain = 14;
    hud.setFrame(f); hud.update(&p, cat, true, true);
    const bool quiet = p.calls.empty();
    f.jamBuffs->push_back({"TnBuffTransformDisruptor", 3});
    hud.setFrame(f); hud.update(&p, cat, true, true);
    const bool disrupted = p.has("_global.ApplyBuff(TnBuffTransformDisruptor,0,3)");
    p.calls.clear();
    f.jamBuffs = std::vector<HudFrame::Buff>{{"TnBuffTransformDisruptor", 2}};
    hud.setFrame(f); hud.update(&p, cat, true, true);
    const bool unapplied = p.has("_global.UnapplyBuff(TnBuffAbilityJammedKillstreak)") && p.calls.size() == 1;
    p.calls.clear();
    f.jamBuffs = std::vector<HudFrame::Buff>{{"TnBuffTransformDisruptor", 3}};   // refreshed (time left jumps up): ReapplyBuff
    hud.setFrame(f); hud.update(&p, cat, true, true);
    check(p.has("_global.ReapplyBuff(TnBuffTransformDisruptor,3)") && p.calls.size() == 1, "hud.jam_buffs_reapply");
    f.jamBuffs = std::vector<HudFrame::Buff>{{"TnBuffTransformDisruptor", 2}};
    hud.update(&p, cat, false, false);   // the movie closes and reopens: the active one again
    p.calls.clear();
    hud.setFrame(f); hud.update(&p, cat, true, true);
    check(applied && quiet && disrupted && unapplied && p.has("_global.ApplyBuff(TnBuffTransformDisruptor,0,2)") &&
          HudController::isJamBuff("TnBuffDifficultyJamAbilitiesRevived") && !HudController::isJamBuff("TnBuffSeeEnemies"), "hud.jam_buffs_apply_unapply");
}

static void testOnlineStartLoadLeave() {
    using T = SessionEvent::Type;
    SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
    OnlineRig r({after("host", o)});
    const bool host = r.ok && hostLobby(r);
    r.fake->inject(memberEv("WFC-AAAA-AAAA-1", "Alpha", true));
    r.fake->inject(memberEv("WFC-BBBB-BBBB-2", "Bravo", true));
    r.frames(2);
    // S14: start -> countdown -> the match loads -> the load gate holds the loading screen until AllLoaded
    r.f().call("Online.BeginLobbyExitCountdown");
    const bool requested = r.fake->count("requestStart") == 1;
    const bool launched = runUntil(*r.rt, [&] { return r.f().hasPendingMatch(); }, 60 * 30);
    r.f().matchLoaded();
    const bool held = r.f().simHeld();   // the World does not step during the load gate (ARCHITECTURE 10.4)
    const bool gate = held && r.f().inLoadGate() && r.f().loading().active && r.fake->count("reportLoaded") == 1 &&
                      r.f().loading().message == "Waiting for 2 players";
    SessionEvent pl = ev(T::PeerLoaded); pl.member.fingerprint = "WFC-AAAA-AAAA-1";
    r.fake->inject(pl);
    r.frames(2);
    const bool one = r.f().inLoadGate() && r.f().loading().message == "Waiting for 1 player";
    check(host && requested && launched && gate && one, "online.S14_start_load_gate",
          std::to_string(host) + std::to_string(requested) + std::to_string(launched) + std::to_string(gate) + std::to_string(one) + " " + r.f().loading().message);
    // S15: Bravo never loads - the session drops it at its load timeout; AllLoaded starts the match
    SessionEvent drop = ev(T::PeerDropped); drop.member.fingerprint = "WFC-BBBB-BBBB-2"; drop.leaveReason = SessionLeaveReason::LoadTimeout;
    r.fake->inject(drop);
    r.frames(2);
    const std::vector<SessionSlotSwap> loadSwaps = r.f().takeSlotSwaps();
    const bool dropped = r.f().onlineMembers().size() == 1 && r.f().loading().message == "Waiting for 0 players" && r.f().takeGameMessages().empty() &&
                         loadSwaps.empty();   // the bot swap is the lockstep driver's SeatLeave now (Systems 2c6cc2b3), not the flow's
    SessionEvent all = ev(T::AllLoaded); all.value = 600;
    r.fake->inject(all);
    r.frames(2);
    check(dropped && !r.f().inLoadGate() && !r.f().loading().active && !r.f().simHeld(), "online.S15_load_timeout_drop");
    // S16: a human leaves mid-match: the original kill-feed line; the slot's bot comes from the driver's SeatLeave (no flow swap)
    SessionEvent left = ev(T::PeerDropped); left.member.fingerprint = "WFC-AAAA-AAAA-1"; left.leaveReason = SessionLeaveReason::Left;
    r.fake->inject(left);
    r.frames(2);
    const std::vector<std::string> lines = r.f().takeGameMessages();
    const std::vector<SessionSlotSwap> swaps = r.f().takeSlotSwaps();
    check(lines.size() == 1 && lines[0] == "Alpha has left the game" && swaps.empty() && r.f().onlineMembers().empty(), "online.S16_peer_leaves_bot_swap",
          lines.empty() ? "" : lines[0]);
}

static void testOnlineMemberCountdown() {
    // The host starts: CountdownStarted runs the same lobby exit countdown on the member.
    using T = SessionEvent::Type;
    SessionLobby l; l.gameModeTag = "TDM"; l.mapId = 508; l.mapSelectionMethod = 1;
    SessionEvent j = ev(T::Joined); j.lobby = l; j.member.fingerprint = "WFC-SELF-SELF-9";
    OnlineRig r({after("join", j)});
    r.f().call("Online.JoinGame", {"WFC-7QK2-M9TD-4H"});
    runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; });
    r.frames(2);
    const bool before = !r.f().lobby().countingDown;
    r.fake->inject(ev(T::CountdownStarted));
    r.frames(2);
    check(r.ok && before && r.f().lobby().countingDown && r.fake->count("requestStart") == 0, "online.member_countdown_follows_host");
}

// A member in its game lobby (TDM 508), optionally carried into the loaded match.
static bool memberIn(OnlineRig& r, bool match) {
    using T = SessionEvent::Type;
    SessionLobby l; l.gameModeTag = "TDM"; l.mapId = 508; l.mapSelectionMethod = 1;
    SessionEvent j = ev(T::Joined); j.lobby = l; j.member.fingerprint = "WFC-SELF-SELF-9";
    r.fake->add(after("join", j));
    r.f().call("Online.JoinGame", {"WFC-7QK2-M9TD-4H"});
    if (!runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; })) return false;
    r.frames(2);
    if (!match) return true;
    r.fake->inject(ev(T::CountdownStarted));
    if (!runUntil(*r.rt, [&] { return r.f().hasPendingMatch(); }, 60 * 30)) return false;
    r.f().matchLoaded();
    r.fake->inject(ev(T::AllLoaded));
    r.frames(2);
    return r.f().level() == LevelKind::Match && !r.f().inLoadGate();
}
// Let the travel out of a match finish (the application normally releases the world).
static void landTravel(OnlineRig& r, LevelKind want) {
    runUntil(*r.rt, [&] { if (r.f().wantsWorldUnload()) r.f().worldUnloaded(); return r.f().level() == want && !r.f().loading().active; }, 60 * 20);
    r.frames(3);
}

static void testOnlineSessionEnd() {
    using T = SessionEvent::Type;
    {   // S17 host lost in the game lobby, then in a match: the party lobby with the original text; no leave()
        OnlineRig r;
        const bool in = r.ok && memberIn(r, false);
        r.fake->inject(ev(T::HostLost));
        landTravel(r, LevelKind::PartyLobby);
        const bool lobbyCase = in && r.f().level() == LevelKind::PartyLobby &&
                               r.popupIs("$Engine.Errors.ConnectionFailed_Title", "$Engine.Errors.ConnectionFailedHostQuit") &&
                               r.f().sessionRole() == GameFlow::SessionRole::Offline && r.fake->count("leave") == 0;
        OnlineRig m;
        const bool inMatch = m.ok && memberIn(m, true);
        m.fake->inject(ev(T::HostLost));
        landTravel(m, LevelKind::PartyLobby);
        check(lobbyCase && inMatch && m.f().level() == LevelKind::PartyLobby && m.f().ui().state() == UIState::InLobby &&
              m.popupIs("$Engine.Errors.ConnectionFailed_Title", "$Engine.Errors.ConnectionFailedHostQuit") && m.fake->count("leave") == 0,
              "online.S17_host_lost", std::to_string(lobbyCase) + std::to_string(inMatch) + " " + levelKindName(m.f().level()));
    }
    {   // S18 connection timed out (member, in a match)
        OnlineRig r;
        const bool in = r.ok && memberIn(r, true);
        SessionEvent c = ev(T::ConnectionLost); c.timedOut = true;
        r.fake->inject(c);
        landTravel(r, LevelKind::PartyLobby);
        check(in && r.popupIs("$Engine.Errors.ConnectionFailed_Title", "$Engine.Errors.ConnectionTimeout") &&
              r.f().sessionRole() == GameFlow::SessionRole::Offline, "online.S18_connection_lost");
    }
    {   // S19 desync: back to the game lobby (still a member) with the out-of-sync text, no tick in it
        OnlineRig r;
        const bool in = r.ok && memberIn(r, true);
        SessionEvent d = ev(T::Desync); d.value = 4321;
        r.fake->inject(d);
        landTravel(r, LevelKind::GameLobby);
        check(in && r.popupIs("$Engine.Errors.ConnectionFailed_Title", "$WfcPC.Online.Desync") &&
              r.f().popup().message.find("4321") == std::string::npos && r.f().sessionRole() == GameFlow::SessionRole::Member, "online.S19_desync");
    }
    {   // S20 idle kick
        OnlineRig r;
        const bool in = r.ok && memberIn(r, true);
        SessionEvent k = ev(T::Kicked); k.kickReason = SessionKickReason::Idle;
        r.fake->inject(k);
        landTravel(r, LevelKind::PartyLobby);
        check(in && r.popupIs("$UIText.MessagePrompts.GenericErrorTitle", "$Engine.Errors.KickedForIdling"), "online.S20_idle_kick");
    }
    {   // S21 after the match: back to the host's lobby, still a member, following the host's map
        OnlineRig r;
        const bool in = r.ok && memberIn(r, true);
        r.f().returnToGameLobby();
        landTravel(r, LevelKind::GameLobby);
        SessionLobby l; l.gameModeTag = "TDM"; l.mapId = 502; l.mapSelectionMethod = 1;
        SessionEvent ls = ev(T::LobbyState); ls.lobby = l;
        r.fake->inject(ls);
        r.frames(3);
        check(in && r.f().sessionRole() == GameFlow::SessionRole::Member && r.f().lobby().mapId == 502 && !r.f().popup().open, "online.S21_match_end_return");
    }
    {   // S22 a member quits mid-match: leave() once, the party lobby, no popup; S23 the display name (D1) on join
        OnlineRig r;
        const bool in = r.ok && memberIn(r, true);
        const bool name = r.fake->last("setMemberInfo") && r.fake->last("setMemberInfo")->info.displayName == r.f().profile().playerName() &&
                          !r.f().profile().playerName().empty();
        r.f().call("Game.QuitToMainMenu");
        const bool memberText = r.f().popup().message == "$UIText.MessagePrompts.LeaveLobbyConfirm";
        r.f().call("MessageBox.OnA");
        landTravel(r, LevelKind::PartyLobby);
        check(in && memberText && r.fake->count("leave") == 1 && r.f().level() == LevelKind::PartyLobby && !r.f().popup().open &&
              r.f().sessionRole() == GameFlow::SessionRole::Offline, "online.S22_member_quits");
        check(in && name, "online.S23_display_name");
    }
    {   // the host's quit warning (PC EXTENSION): leaving ends the game for everyone
        SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
        OnlineRig r({after("host", o)});
        const bool host = r.ok && hostLobby(r);
        r.f().call("Game.QuitToMainMenu");
        check(host && r.f().popup().message == "$WfcPC.Online.HostLeaveConfirm", "online.host_leave_warning");
    }
}

static void testOnlineVoice() {
    using T = SessionEvent::Type;
    {   // V1: voice settings / mutes persist; the defaults write nothing (offline profiles unchanged)
        std::remove("wfc_profile.ini");
        frontend::LocalProfile d;
        d.save();
        std::ifstream a("wfc_profile.ini"); std::stringstream as; as << a.rdbuf(); a.close();
        frontend::LocalProfile c;
        // All Radio (1) differs from the default audience either way (Team Radio 0, the original's rule, after Systems' voice channels)
        c.voice.mode = 2; c.voice.audience = 1; c.voice.pttKey = "Mouse 4"; c.voice.muted = {"WFC-AAAA-AAAA-1", "WFC-BBBB-BBBB-2"};
        c.save();
        std::ifstream b("wfc_profile.ini"); frontend::LocalProfile r; r.loadFrom(b); b.close();
        check(as.str().find("[Voice]") == std::string::npos && r.voice.mode == 2 && r.voice.audience == 1 && r.voice.pttKey == "Mouse 4" &&
              r.voice.muted.size() == 2 && r.voice.muted[1] == "WFC-BBBB-BBBB-2" && d.voice.mode == 1 &&
              d.voice.audience == frontend::LocalProfile::Voice{}.audience, "profile.voice_settings_persisted");
        std::remove("wfc_profile.ini");
    }
    SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
    OnlineRig r({after("host", o)});
    const bool host = r.ok && hostLobby(r);
    // V2: the settings reach the session when the session starts and on each change; push-to-talk only in that mode, on change
    const std::string aud = std::to_string(frontend::LocalProfile::Voice{}.audience);   // Team Radio 0 (Systems' voice channels)
    const bool first = r.fake->count("setVoiceSettings") == 1 && r.fake->last("setVoiceSettings")->arg == "1," + aud + ",,70";
    r.f().profile().voice.mode = 2;
    r.frames(2);
    const bool changed = r.fake->count("setVoiceSettings") == 2 && r.fake->last("setVoiceSettings")->arg == "2," + aud + ",,70";
    r.f().setPushToTalkHeld(true);
    const bool openMicNoPtt = r.fake->count("setPushToTalk") == 0;
    r.f().profile().voice.mode = 1;
    r.frames(2);
    r.f().setPushToTalkHeld(true); r.f().setPushToTalkHeld(true); r.f().setPushToTalkHeld(false);
    check(host && first && changed && openMicNoPtt && r.fake->count("setPushToTalk") == 2 && r.fake->last("setPushToTalk")->arg == "0" &&
          r.rt->catalog().localizeKey("$UIText.PlayerList.Gamercard") == "Mute / Block", "online.V2_voice_settings_and_ptt",
          std::to_string(host) + std::to_string(first) + std::to_string(changed) + std::to_string(openMicNoPtt) + " " +
          r.rt->catalog().localizeKey("$UIText.PlayerList.Gamercard"));
    // V3: HeadsetState per row, the "Near" tag for a proximity speaker, mute from the row's A (local, persisted)
    DataStores& ds = r.rt->dataStores();
    r.fake->inject(memberEv("WFC-AAAA-AAAA-1", "Alpha", true));
    SessionEvent v = ev(T::MemberVoice); v.member.fingerprint = "WFC-AAAA-AAAA-1"; v.headsetState = 2; v.voiceVia = 2;
    r.fake->inject(v);
    r.frames(2);
    ds.poll();
    const std::string talking = ds.call("ReadCollectionValue", {"<CurrentGame:Players>", "PlayerName", "1"}).s;
    const std::string hs = ds.call("ReadCollectionValue", {"<CurrentGame:Players>", "HeadsetState", "1"}).s;
    // the Gamer Card button opens the PC player card (Mute / Block, RE ONLINE_POSTMATCH_PARTY_VOICE_RE); A mutes
    const bool card = r.f().call("Online.ShowGamerCardUI", {"Alpha  PC"}).b && r.f().popup().open && r.f().popup().title == "Alpha";
    r.f().popupButton('A');
    const bool muteCall = card && r.fake->last("setMuted") && r.fake->last("setMuted")->arg == "WFC-AAAA-AAAA-1,1" &&
                          r.f().profile().voice.muted.size() == 1;
    ds.poll();
    const std::string mutedName = ds.call("ReadCollectionValue", {"<CurrentGame:Players>", "PlayerName", "1"}).s;
    const std::string mutedHs = ds.call("ReadCollectionValue", {"<CurrentGame:Players>", "HeadsetState", "1"}).s;
    r.f().call("Online.ShowGamerCardUI", {"Alpha  PC"});
    r.f().popupButton('A');   // Unmute
    const bool unmuted = r.fake->last("setMuted")->arg == "WFC-AAAA-AAAA-1,0" && r.f().profile().voice.muted.empty();
    r.f().call("Online.ShowGamerCardUI", {"Alpha  PC"});
    r.f().popupButton('X');   // Block: muted, remembered on this PC (a local list, no server report)
    LocalProfile saved; saved.load();
    const bool blocked = r.fake->last("setMuted")->arg == "WFC-AAAA-AAAA-1,1" && saved.voice.blocked.size() == 1 &&
                         saved.voice.blocked[0] == "WFC-AAAA-AAAA-1";
    r.f().call("Online.ShowGamerCardUI", {"Alpha  PC"});
    r.f().popupButton('X');   // Unblock
    const bool unblocked = r.fake->last("setMuted")->arg == "WFC-AAAA-AAAA-1,0" && r.f().profile().voice.blocked.empty();
    check(talking == "Alpha  PC  Near" && hs == "2" && muteCall && mutedName == "Alpha  PC" && mutedHs == "1" && unmuted && blocked &&
          unblocked, "online.V3_headset_near_mute",
          talking + " / " + hs + " / " + mutedName + " / " + mutedHs + " / " + std::to_string(card) + std::to_string(unmuted) +
          std::to_string(blocked) + std::to_string(unblocked));
}

static void testCampaignObjectivesManifest() {
    // AssetTools' D1_ORB objectives: the bank and setters load; a bank setter shows its text; the out-of-bank D1_ORB_OBJ08
    // setter is refused (the original logs it, shows nothing); a clearing setter clears.
    FrontendRuntime rt;
    check(rt.init(), "objmanifest.init");
    CampaignObjectives o;
    const std::string path = CampaignObjectives::manifestFor(frontend::Catalog::defaultManifestRoot(), "D1_ORB");
    const bool loaded = o.loadManifest(path) && !o.bank().empty() && o.setterCount() > 0;
    const bool first = o.setCurrent(o.bank().front(), rt.catalog()) && !o.activeText().empty();
    const int before = o.current();
    const bool refused = !o.fireSetter("D1_ORB_Base_m.TheWorld.PersistentLevel.Main_Sequence.Objective_Markers.SeqAct_TnSetCurrentObjective_11332", rt.catalog()) &&
                         o.current() == before;
    check(loaded && first && refused && !o.fireSetter("No.Such.Setter", rt.catalog()), "campaign.objectives_manifest_d1",
          std::to_string(loaded) + std::to_string(first) + std::to_string(refused) + " bank " + std::to_string(o.bank().size()));
    // A1: its seven " " setters are no-ops (the objective stays up); an empty-key clearing setter would clear
    CampaignObjectives a;
    bool noop = a.loadManifest(CampaignObjectives::manifestFor(frontend::Catalog::defaultManifestRoot(), "A1_IAC")) &&
                a.setCurrent(a.bank().front(), rt.catalog());
    const std::string shown = a.activeText();
    int noops = 0;
    {
        std::ifstream f(frontend::openPath(CampaignObjectives::manifestFor(frontend::Catalog::defaultManifestRoot(), "A1_IAC")), std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        assets::Json j;
        if (assets::Json::parse(ss.str(), j))
            for (size_t i = 0; i < j["setters"].size(); ++i)
                if (j["setters"][i]["key_raw"].asString() == " ") {
                    ++noops;
                    noop = noop && a.fireSetter(j["setters"][i]["action"].asString(), rt.catalog()) && a.current() == 0 && a.activeText() == shown;
                }
    }
    check(noop && noops == 7 && !shown.empty(), "campaign.objectives_whitespace_noop", std::to_string(noops));
}

static void testOnlineMatchSide() {
    // The match side of Remote participants (ONLINE_MP_FRONTEND_TESTS.md section 4, the frontend half of WFC_SESSIONTEST)
    using T = SessionEvent::Type;
    SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
    OnlineRig r({after("host", o)});
    const bool host = r.ok && hostLobby(r);
    r.fake->inject(memberEv("WFC-AAAA-AAAA-1", "Alpha", true));
    r.fake->inject(memberEv("WFC-BBBB-BBBB-2", "Bravo", true));
    r.frames(2);
    r.f().call("Online.BeginLobbyExitCountdown");
    const bool launched = runUntil(*r.rt, [&] { return r.f().hasPendingMatch(); }, 60 * 30);
    // M1: the launch carries the host's slots (host first, "" id), self ("" on the host) and every member's name
    const MatchLaunch& ml = r.f().currentMatch();
    const bool slots = launched && ml.onlineSlots.size() == 3 && ml.onlineSlots[0].kind == SessionSlot::Kind::Human &&
                       ml.onlineSlots[0].member.fingerprint.empty() && ml.onlineSlots[1].member.fingerprint == "WFC-AAAA-AAAA-1" &&
                       ml.self.fingerprint.empty() && ml.onlineNames.size() == 3 && ml.onlineNames[2].second == "Bravo";
    check(host && slots, "online.M1_launch_slots", std::to_string(ml.onlineSlots.size()) + "/" + std::to_string(ml.onlineNames.size()));
    r.f().matchLoaded();
    SessionEvent all = ev(T::AllLoaded); all.value = 600;
    r.fake->inject(all);
    r.frames(2);
    // M2: Gameplay's roster with Remote participants -> scoreboard rows with platform tags; HeadsetState / "Near" for the
    // HUD talker list (Hud_GFX sprite 573 lists the rows with HeadsetState 2 every 500 ms)
    auto roster = [&](bool alphaRemote, const std::string& alphaName) {
        MatchValues v; v.valid = true; v.myTeam = 0;
        MatchValues::Player me; me.name = "Self"; me.team = 0; me.local = true; me.score = 3;
        MatchValues::Player a; a.name = alphaName; a.team = 1; a.score = 7; a.kills = 2; a.remote = alphaRemote; a.bot = !alphaRemote;
        if (alphaRemote) a.member = "WFC-AAAA-AAAA-1";
        MatchValues::Player b; b.name = "Bravo"; b.team = 0; b.score = 1; b.remote = true;   // no sessionMember: matched by name
        MatchValues::Player bot; bot.name = "Ratchet"; bot.team = 1; bot.bot = true;
        v.players = {me, a, b, bot};
        r.f().setMatchValues(v);
    };
    roster(true, "Alpha");
    SessionEvent vo = ev(T::MemberVoice); vo.member.fingerprint = "WFC-AAAA-AAAA-1"; vo.headsetState = 2; vo.voiceVia = 2;
    r.fake->inject(vo);
    r.frames(2);
    DataStores& ds = r.rt->dataStores();
    ds.poll();
    auto cell = [&](const char* col, int row) { return ds.call("ReadCollectionValue", {"<CurrentGame:Players>", col, std::to_string(row)}).s; };
    const std::string selfName = cell("PlayerName", 0);
    const bool rows = selfName.size() > 4 && selfName.substr(selfName.size() - 4) == "  PC" && cell("HeadsetState", 0) == "1" &&
                      cell("PlayerName", 1) == "Alpha  PC  Near" && cell("HeadsetState", 1) == "2" && cell("Score", 1) == "7" &&
                      cell("PlayerName", 2) == "Bravo  PC" && cell("HeadsetState", 2) == "0" && cell("PlayerName", 3) == "Ratchet";
    r.f().setPushToTalkHeld(true);
    ds.poll();
    const bool selfTalking = cell("HeadsetState", 0) == "2";
    r.f().setPushToTalkHeld(false);
    check(rows && selfTalking, "online.M2_match_rows_talkers", selfName + " / " + cell("PlayerName", 1) + " / " + cell("PlayerName", 2));
    // M3: Alpha leaves -> one kill-feed line and a swap by member; Gameplay keeps the slot (same index, score) as a bot, so
    // the row stays with its score and loses the tag / voice. Bravo is idle-kicked by the host rule: the same line once.
    SessionEvent left = ev(T::PeerDropped); left.member.fingerprint = "WFC-AAAA-AAAA-1"; left.leaveReason = SessionLeaveReason::Left;
    r.fake->inject(left);
    SessionEvent idle = ev(T::PeerDropped); idle.member.fingerprint = "WFC-BBBB-BBBB-2"; idle.leaveReason = SessionLeaveReason::Kicked;
    r.fake->inject(idle);
    r.frames(2);
    const std::vector<std::string> lines = r.f().takeGameMessages();
    const std::vector<SessionSlotSwap> swaps = r.f().takeSlotSwaps();
    roster(false, "Alpha");
    ds.poll();
    const bool kept = cell("PlayerName", 3) == "Ratchet" && cell("PlayerName", 2) == "Bravo";   // a Remote row whose member left: the plain name
    r.f().onlineIdleKick("WFC-BBBB-BBBB-2");   // Gameplay's RemoteIdle on the host -> the session kicks that peer (idle)
    const bool kicked = r.fake->last("kick") && r.fake->last("kick")->arg.find("WFC-BBBB-BBBB-2") != std::string::npos;
    check(kicked, "online.M3b_idle_kick_to_session", r.fake->last("kick") ? r.fake->last("kick")->arg : std::string());
    check(lines.size() == 2 && lines[0] == "Alpha has left the game" && lines[1] == "Bravo has left the game" && swaps.empty() && kept && cell("PlayerName", 1) == "Alpha" && cell("Score", 1) == "7" &&
          cell("HeadsetState", 1) == "0" && r.f().takeGameMessages().empty(), "online.M3_swap_rows_idle_line",
          std::to_string(lines.size()) + " " + cell("PlayerName", 1));
}

static void testIconicMode() {
    using T = SessionEvent::Type;
    SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
    OnlineRig r({after("host", o)});
    const bool host = r.ok && hostLobby(r);
    auto cz = [&](const char* fn, std::vector<std::string> a = {}) { return r.rt->bridge("UI_GFxCharacterSelect_p.CharacterSelect_GFX_1", fn, a).s; };
    // I1: custom mode (the versus default) is unchanged: the wheel / Customize lists are the custom slots
    const std::string custom = cz("Customize.GetPlayableCharacters");
    r.f().profile().bots.iconic = true;   // the PC EXTENSION row (Game Mode Options), On
    const std::string iconic = cz("Customize.GetPlayableCharacters");
    // authored PresetIconicCharacters order (AirRaid first); LockedCharacter presets (Arcee, Jazz...) hidden on a fresh
    // profile; the car soldiers never listed
    const bool lists = custom == "Scout,Scientist,Soldier,Leader" && iconic.rfind("AirRaid,Barricade,Brawl,Breakdown,Bumblebee", 0) == 0 &&
                       iconic.find("Optimus Prime") != std::string::npos && iconic.find("Arcee") == std::string::npos &&
                       iconic.find("CarSoldier") == std::string::npos && cz("Customize.GetCharacterSpecialty", {"Bumblebee"}) == "Scout" &&
                       cz("Customize.LocalizeUniqueIconicId", {"Optimus Prime"}) == "Optimus Prime";
    check(host && lists, "iconic.I1_playable_set", custom + " | " + iconic);
    // I2: selecting an iconic character hands Gameplay type 1 with the preset's chassis per faction
    cz("Customize.SelectCharacter", {"Bumblebee", "0"});
    const GameFlow::SelectedCharacter& sel = r.f().selectedCharacter();
    check(sel.valid && sel.type == 1 && sel.name == "Bumblebee" && sel.chassis[0] == "Car" && sel.chassis[1] == "Car" && sel.specialty == "Scout",
          "iconic.I2_select_type1", sel.name + " " + std::to_string(sel.type) + " " + sel.chassis[0]);
    // I3: claims (online): another player's pick, any team, is listed as claimed (RE: the whole PRIArray); the same pick
    // twice holds the gate at "waiting for character choice" (6) until the later one picks another
    SessionEvent a = memberEv("WFC-AAAA-AAAA-1", "Alpha", true);
    a.info.iconicCharacter = "Bumblebee";
    r.fake->inject(a);
    r.frames(2);
    if (!r.f().onlineMembers().empty() && r.f().onlineMembers()[0].team == r.f().lobby().localTeam) r.f().call("Online.SwitchTeam");
    r.frames(2);   // Alpha on the other team: still claimed
    DataStores& ds = r.rt->dataStores();
    ds.poll();
    const bool claimed = ds.call("ReadValue", {"<CurrentGame:ClaimedIconicCharacters>"}).s == "Bumblebee" &&
                         ds.call("ReadValue", {"<CurrentGame:_bIconicMode>"}).s == "1";
    const int dup = r.f().lobby().lobbyStatus;
    cz("Customize.SelectCharacter", {"Megatron", "0"});
    r.frames(2);
    const int ok = r.f().lobby().lobbyStatus;
    r.f().profile().bots.iconic = false;
    check(claimed && dup == 6 && ok != 6 && cz("Customize.GetPlayableCharacters") == "Scout,Scientist,Soldier,Leader", "iconic.I3_claims_any_team",
          std::to_string(claimed) + " " + std::to_string(dup) + " " + std::to_string(ok));
}

static void testVoiceUi() {
    using T = SessionEvent::Type;
    // V4: the Mic Test meter - the session's level as ten marks
    check(GameFlow::micMeterText(0.0f) == ".........." && GameFlow::micMeterText(0.5f) == "|||||....." &&
          GameFlow::micMeterText(1.4f) == "||||||||||", "voice.V4_mic_meter_text");
    {
        SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
        OnlineRig r({after("host", o)});
        r.fake->mic = 0.5f;
        check(r.ok && r.f().micLevel() == 0.5f && r.rt->dataStores().call("ReadValue", {"<PCSettings:VoiceMicTest>"}).s == "0", "voice.V4_mic_level");
    }
    // V5: a remembered mute (the profile's [Voice] Muted, saved / loaded in profile.voice_settings_persisted) reaches the
    // session again when that member is seen in a new session
    {
        SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
        OnlineRig r({after("host", o)});
        r.f().profile().voice.muted = {"WFC-ZZZZ-ZZZZ-9"};
        const bool loaded = true;
        const bool host = r.ok && hostLobby(r);
        r.fake->inject(memberEv("WFC-ZZZZ-ZZZZ-9", "Zed", true));
        r.frames(2);
        check(loaded && host && r.fake->last("setMuted") && r.fake->last("setMuted")->arg == "WFC-ZZZZ-ZZZZ-9,1", "voice.V5_mute_persists",
              std::to_string(loaded) + std::to_string(host));
        r.f().profile().voice.muted.clear();
        r.f().profile().save();
    }
    // V6 / V7: the push-to-talk key capture ("Custom..."): the prompt; the key that chose it is ignored until released;
    // the next key is stored by name; Esc and the Cancel button keep the key; no push-to-talk while choosing
    SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
    OnlineRig r({after("host", o)});
    const bool host = r.ok && hostLobby(r);
    std::vector<uint16_t> keys;
    r.rt->keyProbe = [&]() -> uint16_t { if (keys.empty()) return 0; const uint16_t k = keys.front(); keys.erase(keys.begin()); return k; };
    DataStores& ds = r.rt->dataStores();
    const std::string before = r.f().profile().voice.pttKey;
    keys = {0x0D, 0x0D};   // Enter still held from choosing "Custom..."
    ds.call("WriteValue", {"<PCSettings:VoicePushToTalkKey>", "__custom"});
    const bool prompt = r.f().pttCaptureActive() && r.f().popup().open &&
                        r.f().popup().message == r.rt->catalog().localizeKey("$WfcPC.Voice.PressKeyPrompt");
    r.frames(2);
    const bool waited = r.f().pttCaptureActive() && r.f().profile().voice.pttKey == before;
    const int pttCalls = r.fake->count("setPushToTalk");
    r.f().setPushToTalkHeld(true);
    check(host && prompt && waited && r.fake->count("setPushToTalk") == pttCalls, "voice.V6_no_ptt_while_choosing");
    keys = {0, 0x70};   // released, then F1
    r.frames(3);
    const bool set = !r.f().pttCaptureActive() && !r.f().popup().open && r.f().profile().voice.pttKey == "F1" &&
                     platform::virtualKeyForName("F1") == 0x70 && platform::nameForVirtualKey(0x01).empty();
    ds.call("WriteValue", {"<PCSettings:VoicePushToTalkKey>", "__custom"});
    keys = {0, 0x1B};   // Esc
    r.frames(3);
    const bool esc = !r.f().pttCaptureActive() && r.f().profile().voice.pttKey == "F1";
    ds.call("WriteValue", {"<PCSettings:VoicePushToTalkKey>", "__custom"});
    r.f().popupButton('B');   // Cancel
    const bool cancel = !r.f().pttCaptureActive() && !r.f().popup().open && r.f().profile().voice.pttKey == "F1";
    r.f().profile().voice.pttKey = "V";
    r.f().profile().save();
    check(set && esc && cancel, "voice.V7_ptt_key_capture", std::to_string(set) + std::to_string(esc) + std::to_string(cancel));
}

// A rig whose fresh profile has not answered the one-time "Online Play" prompt yet.
struct FirstRunRig : OnlineRig {
    FirstRunRig() : OnlineRig() {
        if (!ok) return;
        f().profile().onlineFirstRunShown = false;
        frames(3);
    }
};

static void testMemberSeatOrder() {
    // Cross-perspective (Gameplay's seat-order fix: game::MatchLaunch humans + localSeat from these inputs): a member's launch
    // carries the host's human seats in the host's order - host, this member, Alpha - with this member as an entry at seat 1
    // (not excluded), the host's names, and the launch record's roster overriding a stale lobby.
    using T = SessionEvent::Type;
    OnlineRig r;
    SessionLobby l; l.gameModeTag = "TDM"; l.mapId = 508; l.mapSelectionMethod = 1;
    auto human = [](const char* fp, int team) { SessionSlot s; s.kind = SessionSlot::Kind::Human; s.member.fingerprint = fp; s.team = team; return s; };
    auto info = [](const char* fp, const char* name) { SessionMemberInfo i; i.id.fingerprint = fp; i.displayName = name; i.hasCharacter = true; return i; };
    l.slots = {human("", 0), human("WFC-SELF-SELF-9", 1), human("WFC-AAAA-AAAA-1", 0)};
    l.members = {info("", "Host"), info("WFC-SELF-SELF-9", "Me"), info("WFC-AAAA-AAAA-1", "Alpha")};
    SessionEvent j = ev(T::Joined); j.lobby = l; j.member.fingerprint = "WFC-SELF-SELF-9";
    r.fake->add(after("join", j));
    r.f().call("Online.JoinGame", {"WFC-7QK2-M9TD-4H"});
    const bool in = runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; });
    r.frames(2);
    SessionEvent cs = ev(T::CountdownStarted); cs.lobby = l; cs.seed = 77;
    cs.launchUrl = "MP_IAC_Streets_Base_m?GameModeTag=TDM?GameTeamStatus=3?MapId=508";
    r.fake->inject(cs);
    const bool launched = runUntil(*r.rt, [&] { return r.f().hasPendingMatch(); }, 60 * 30);
    const MatchLaunch& ml = r.f().currentMatch();
    int seat = -1, humans = 0;
    for (const SessionSlot& sl : ml.onlineSlots) {
        if (sl.kind != SessionSlot::Kind::Human) continue;
        if (sl.member == ml.self) seat = humans;
        ++humans;
    }
    std::string hostName;
    for (const auto& n : ml.onlineNames) if (n.first.fingerprint.empty()) hostName = n.second;
    check(in && launched && humans == 3 && seat == 1 && ml.self.fingerprint == "WFC-SELF-SELF-9" && hostName == "Host" && ml.seed == 77,
          "online.L3_member_seat_order", std::to_string(humans) + " seat " + std::to_string(seat));
}

static void testLobbyWaitingOne() {
    // "Finding `p more player" (singular) for exactly one missing, the plural otherwise (RE ONLINE_RULES_CHECK_RE)
    FrontendRuntime rt;
    rt.init();
    const std::string one = rt.catalog().localize("TransGame", "TnGameReplicationInfoGameLobby", "WaitingForOnePlayerStatus");
    const std::string many = rt.catalog().localize("TransGame", "TnGameReplicationInfoGameLobby", "WaitingForPlayersStatus");
    check(!one.empty() && one != many && one.find("`p") != std::string::npos, "lobby.waiting_one_player_text", one);
}

static void testLensFlaresSetting() {
    // "Lens Flares: Off / Subtle / Original": default Original (2), not written while default; a change persists and the
    // data store follows
    std::remove("wfc_profile.ini");
    frontend::LocalProfile d;
    d.save();
    std::ifstream a("wfc_profile.ini"); std::stringstream as; as << a.rdbuf(); a.close();
    frontend::LocalProfile c;
    c.display.lensFlares = 1;
    c.save();
    std::ifstream b("wfc_profile.ini"); frontend::LocalProfile r; r.loadFrom(b); b.close();
    std::remove("wfc_profile.ini");
    FrontendRuntime rt;
    rt.init();
    DataStores& ds = rt.dataStores();
    const bool def = ds.call("ReadValue", {"<PCSettings:LensFlares>"}).s == "2";
    ds.call("WriteValue", {"<PCSettings:LensFlares>", "0"});
    const bool off = rt.flow().profile().display.lensFlares == 0 && ds.call("ReadValue", {"<PCSettings:LensFlares>"}).s == "0";
    rt.flow().profile().display.lensFlares = 2;
    check(d.display.lensFlares == 2 && as.str().find("LensFlares") == std::string::npos && r.display.lensFlares == 1 && def && off &&
          rt.catalog().localizeKey("$WfcPC.Settings.LensFlares") == "Lens Flares", "settings.lens_flares_row");
}

static void testRespawnUiLifetime() {
    // User playtest 2026-10-23 #2: the respawn screen exists only while dead and spectating.
    std::vector<std::string> open;
    UIController ui;
    ui.initialize(UIControllerClass::Multiplayer, [&](const std::string& m) { open.push_back(m); }, [&] { if (!open.empty()) open.pop_back(); },
                  [](bool) {}, nullptr, false);
    ui.setGRIAvailable(true);
    ui.onUIEvent(5);                                   // first spawn -> InGame
    const std::string respawn = ui.spectatingUI();
    auto has = [&](const std::string& m) { return std::find(open.begin(), open.end(), m) != open.end(); };
    ui.onUIEvent(4);                                   // dead -> Spectating: the respawn screen
    const bool shown = ui.state() == UIState::Spectating && has(respawn);
    ui.onUIEvent(6);                                   // pause while dead
    ui.onUIEvent(5);                                   // respawned under the pause menu
    ui.onUIEvent(7);                                   // resume
    const bool resumedAlive = ui.state() == UIState::InGame && !has(respawn);
    ui.onUIEvent(6);                                   // pause while alive
    ui.onUIEvent(4);                                   // killed under the pause menu
    ui.onUIEvent(7);                                   // resume -> spectating with the respawn screen
    const bool resumedDead = ui.state() == UIState::Spectating && has(respawn);
    ui.onUIEvent(5);
    const bool closed = ui.state() == UIState::InGame && !has(respawn);
    ui.onUIEvent(4); ui.onUIEvent(6); ui.onUIEvent(9);  // the match ends under the pause menu
    check(shown && resumedAlive && resumedDead && closed && ui.state() == UIState::GameEnded && !has(respawn), "ui.respawn_screen_lifetime",
          std::to_string(shown) + std::to_string(resumedAlive) + std::to_string(resumedDead) + std::to_string(closed));
}

static void testCampaignC1C4() {
    // CAMPAIGN_FRONTEND_PLAN C1 / C4: Solo -> Mission Select opens the SOLO campaign lobby; difficulty is the profile's
    // setting 12 (default Medium, clamped 0-2, saved); CampaignSelect's chapter fields come from the profile (each faction's
    // first chapter open, never completed, checkpoint 0); no checkpoint saves yet (Resume rows hidden).
    std::remove("wfc_profile.ini");
    {
        FrontendRuntime rt;
        if (!rt.init()) { check(false, "campaign.C1_C4", "init"); return; }
        GameFlow& f = rt.flow();
        runUntil(rt, [&] { return f.frontEndStarted(); });
        DataStores& ds = rt.dataStores();
        auto rd = [&](const char* m) { return ds.call("ReadValue", {m}).s; };
        const bool fields = rd("<OnlinePlayerData:ProfileData.A1Available>") == "1" && rd("<OnlinePlayerData:ProfileData.D1Available>") == "1" &&
                            rd("<OnlinePlayerData:ProfileData.A2Available>") == "0" && rd("<OnlinePlayerData:ProfileData.D3Difficulty>") == "-1" &&
                            rd("<OnlinePlayerData:ProfileData.A1Checkpoint>") == "0";
        const bool diffDefault = f.call("Game.GetGameDifficulty", {}).n == 1.0;
        f.call("Game.SetGameDifficulty", {"5"});
        const bool clamped = f.call("Game.GetGameDifficulty", {}).n == 2.0;
        f.call("Console.SaveProfileSettings", {});
        const bool noResume = !f.call("Game.GetCheckpointFileStatus", {"0"}).b && !f.call("Game.GetCheckpointFileStatus", {"1"}).b;
        f.call("Online.OpenCampaignGameLobby", {});
        const bool lobby = runUntil(rt, [&] { return f.level() == LevelKind::GameLobby && !f.loading().active; }) &&
                           f.lobby().gameModeTag == "SOLO" && rd("<CurrentGame:GameTeamStatus>") == "GTS_CampaignGame";
        LocalProfile back;
        back.load();
        check(fields && diffDefault && clamped && noResume && lobby && back.campaign.difficulty == 2, "campaign.C1_C4",
              std::to_string(fields) + std::to_string(diffDefault) + std::to_string(clamped) + std::to_string(noResume) + std::to_string(lobby) +
              std::to_string(back.campaign.difficulty));
    }
    std::remove("wfc_profile.ini");
}

static void testCampaignC3() {
    // CAMPAIGN_FRONTEND_PLAN C3: TransLevels.ini checkpoints (84 thumbs: 8 chapters x 10 + A5 / D5 x 2; thumb 0 = the chapter
    // start, no path), their TransGame.int names, the chapters' iconic trios; SetSelectedCheckpoint -> the launch URL's
    // ?Checkpoint= in a campaign lobby; a chapter change clears it.
    std::remove("wfc_profile.ini");
    FrontendRuntime rt;
    if (!rt.init()) { check(false, "campaign.C3_checkpoints", "init"); return; }
    const Catalog& cat = rt.catalog();
    int d1 = 0; bool d1Start = false, named = false;
    for (const CheckpointInfo& cp : cat.checkpoints()) {
        if (cp.mapId == 101) { if (d1 == 0) d1Start = cp.checkpointPath.empty(); ++d1; }
        if (cp.name == "D1_CP02") named = cp.friendlyName == "Entering Orbital Station" && cp.checkpointPath.find("Checkpoint_5199") != std::string::npos;
    }
    const MapInfo* d1map = cat.mapById(101);
    const bool trio = d1map && d1map->campaignCharacters == std::vector<std::string>({"Brawl", "Barricade", "Megatron"});
    GameFlow& f = rt.flow();
    runUntil(rt, [&] { return f.frontEndStarted(); });
    f.call("Online.OpenCampaignGameLobby", {});
    runUntil(rt, [&] { return f.level() == LevelKind::GameLobby && !f.loading().active; });
    f.call("Online.SetSelectedMapID", {"101"});
    f.call("Online.SetSelectedCheckpoint", {"D1_ORB_Base_m.TheWorld|PersistentLevel.Checkpoint_5199"});
    DataStores& ds = rt.dataStores();
    const bool selected = ds.call("ReadValue", {"<CurrentGame:SelectedCheckpoint>"}).s.find("Checkpoint_5199") != std::string::npos;
    f.call("Online.SetSelectedMapID", {"102"});
    const bool cleared = ds.call("ReadValue", {"<CurrentGame:SelectedCheckpoint>"}).s.empty();
    f.call("Online.SetSelectedMapID", {"101"});
    f.call("Online.SetSelectedCheckpoint", {"D1_ORB_Base_m.TheWorld|PersistentLevel.Checkpoint_5199"});
    // the launch URL carries the checkpoint; the start itself is refused while the chapter has no rebuild runtime data
    const Url lu = Url::parse(f.buildLaunchUrl());
    f.call("Online.BeginLobbyExitCountdown", {});
    rt.update(platform::InputFrame{}, 1.0f / 60.0f);
    const bool launched = lu.map() == "D1_ORB_Base_m" && lu.option("Checkpoint").find("Checkpoint_5199") != std::string::npos &&
                          lu.intOption("MapId", -1) == 101 && (cat.mapById(101)->hasRequiredAssets || !f.lobby().countingDown) &&
                          lu.intOption("WfcDifficulty", -1) == f.profile().campaign.difficulty;   // Gameplay's chapter rules
    check(cat.checkpoints().size() == 84 && d1 == 10 && d1Start && named && trio && selected && cleared && launched, "campaign.C3_checkpoints",
          std::to_string(cat.checkpoints().size()) + " d1=" + std::to_string(d1) + " " + std::to_string(d1Start) + std::to_string(named) +
          std::to_string(trio) + std::to_string(selected) + std::to_string(cleared) + std::to_string(launched) + " " + f.currentMatch().url.toString());
    std::remove("wfc_profile.ini");
}

static void testCampaignC2C5() {
    // C2 / C5 + the profile's CompleteMission / ReachedCheckpoint (RE): the next chapter in the faction's order unlocks (A5
    // wraps to A1), the best difficulty only rises, a checkpoint clears the resume slot; in a campaign lobby the character
    // list is the chapter's trio, a chapter change clears a pick outside it, and the host can start.
    LocalProfile::Campaign c;
    c.completeMission(101, 1);
    c.completeMission(101, 0);
    const bool unlock = c.complete[5] && c.bestDifficulty[5] == 1 && c.available[6] && c.resumeMap[1] == 102 && !c.available[7];
    c.completeMission(100, 2);
    const bool wrap = c.available[0] && c.resumeMap[0] == 96 && c.nextAvailableChapter(101) == 102 && c.nextAvailableChapter(102) == 102;
    c.reachedCheckpoint(102, 4); c.reachedCheckpoint(102, 2);
    const bool cp = c.checkpoint[6] == 4 && c.resumeMap[1] == -1;
    std::remove("wfc_profile.ini");
    FrontendRuntime rt;
    if (!rt.init()) { check(false, "campaign.C2_C5", "init"); return; }
    GameFlow& f = rt.flow();
    runUntil(rt, [&] { return f.frontEndStarted(); });
    f.call("Online.OpenCampaignGameLobby", {});
    runUntil(rt, [&] { return f.level() == LevelKind::GameLobby && !f.loading().active; });
    f.call("Online.SetSelectedMapID", {"101"});
    f.call("Online.SetSelectedMapID", {"101"});
    const std::string trio = rt.bridge("UI_GFxCharacterSelect_p.CharacterSelect_GFX_1", "Customize.GetPlayableCharacters", {}).s;
    const bool trioOk = trio.find("Megatron") != std::string::npos && trio.find("Brawl") != std::string::npos &&
                        trio.find("Barricade") != std::string::npos && trio.find("Starscream") == std::string::npos;
    rt.bridge("UI_GFxCharacterSelect_p.CharacterSelect_GFX_1", "Customize.SelectCharacter", {"Brawl", "1"});
    const bool picked = f.selectedCharacter().valid;
    f.call("Online.SetSelectedMapID", {"102"});   // D2: Starscream / Skywarp / Thundercracker
    const bool cleared = !f.selectedCharacter().valid;
    const bool canStart = rt.dataStores().call("ReadBoolValue", {"<CurrentGame:CanHostRequestGameStart>"}).b;
    check(unlock && wrap && cp && trioOk && picked && cleared && canStart, "campaign.C2_C5",
          std::to_string(unlock) + std::to_string(wrap) + std::to_string(cp) + std::to_string(trioOk) + std::to_string(picked) +
          std::to_string(cleared) + std::to_string(canStart) + " trio=" + trio);
    std::remove("wfc_profile.ini");
}

static void testSmokeWaterRows() {
    // the Graphics rows Smoke / Water (PC EXTENSION, Rendering setSmokeMode / setWaterMode): default 0 Original, written only
    // when Enhanced, round-trip through the data store and the profile
    std::remove("wfc_profile.ini");
    FrontendRuntime rt;
    if (!rt.init()) { check(false, "settings.smoke_water_rows", "init"); return; }
    DataStores& ds = rt.dataStores();
    const bool defaults = ds.call("ReadValue", {"<PCSettings:Smoke>"}).s == "0" && ds.call("ReadValue", {"<PCSettings:Water>"}).s == "0";
    ds.call("WriteValue", {"<PCSettings:Smoke>", "1"});
    ds.call("WriteValue", {"<PCSettings:Water>", "5"});
    rt.flow().profile().save();
    LocalProfile back; back.load();
    check(defaults && back.display.smoke == 1 && back.display.water == 1, "settings.smoke_water_rows",
          std::to_string(defaults) + std::to_string(back.display.smoke) + std::to_string(back.display.water));
    std::remove("wfc_profile.ini");
}

static void testEscalationNoXp() {
    // RE ESCALATION_WAVES_RE 5: TnGameReplicationInfoSurvival.CanGainXp is false - Escalation earns no XP (the end screen's bars
    // show no gain); an offline versus match keeps the PC progression rule
    std::remove("wfc_profile.ini");
    FrontendRuntime rt;
    if (!rt.init()) { check(false, "escalation.no_xp", "init"); return; }
    GameFlow& f = rt.flow();
    runUntil(rt, [&] { return f.frontEndStarted(); });
    f.call("Online.OpenPartyLobby", {"GTS_TeamGame"});
    runUntil(rt, [&] { return f.level() == LevelKind::PartyLobby && !f.loading().active; });
    f.call("Online.EditGameMode", {"TDM"});
    const bool versus = f.canGainXp();
    f.call("Online.EditGameMode", {"SV"});
    const bool escalation = f.canGainXp();
    check(versus && !escalation, "escalation.no_xp", std::to_string(versus) + std::to_string(escalation));
    std::remove("wfc_profile.ini");
}

static void testCampaignC6() {
    // C6 (RE CAMPAIGN_RUNTIME_EVENTS_RE), with this test as the fake runtime (no chapter loads yet: a TDM match, campaign forced):
    // the Coop UI controller (in-map trio select, no countdown screen); ClientReachedCheckpoint (an indexed checkpoint advances
    // the profile, -1 does not); CampaignGameOver -> UI 14 -> MISSION FAILED; Restart From Last Checkpoint -> the restart request,
    // the respawn closes the screen; ClientCompleteMission -> CompleteMission saved + back to the lobby (CP / SOLO).
    std::remove("wfc_profile.ini");
    FrontendRuntime rt;
    if (!rt.init()) { check(false, "campaign.C6_runtime_events", "init"); return; }
    GameFlow& f = rt.flow();
    runUntil(rt, [&] { return f.frontEndStarted(); });
    f.call("Online.OpenPartyLobby", {"GTS_TeamGame"});
    runUntil(rt, [&] { return f.level() == LevelKind::PartyLobby && !f.loading().active; });
    f.call("Online.EditGameMode", {"TDM"});
    f.call("Online.PlayPrivateGame", {"TDM"});
    runUntil(rt, [&] { return f.level() == LevelKind::GameLobby && !f.loading().active; });
    f.call("Online.SetSelectedMapID", {"508"});
    f.call("Online.SetSelectedMapID", {"508"});
    f.call("Online.BeginLobbyExitCountdown", {});
    runUntil(rt, [&] { return f.hasPendingMatch(); }, 60 * 20);
    f.setCampaignForTest(true);
    f.matchLoaded();
    f.onUIEvent(3);
    const std::string coopWhy = std::string(uiControllerClassName(f.ui().cls())) + "/" + uiStateName(f.ui().state()) + "/" + f.ui().openMovie() +
                                "/" + levelKindName(f.level()) + "/" + (f.hasPendingMatch() ? "pending" : "-");
    const bool coop = f.ui().cls() == UIControllerClass::Coop && f.ui().state() == UIState::WaitingOnGameStart &&
                      f.ui().openMovie() == "UI_GFxCharacterSelect_p.CharacterSelect_GFX_1";
    f.onUIEvent(5);
    const bool inGame = f.ui().state() == UIState::InGame;
    f.campaignCheckpointReached(101, -1);
    const bool notIndexed = f.profile().campaign.checkpoint[5] == 0;
    f.campaignCheckpointReached(101, 3);
    const bool indexed = f.profile().campaign.checkpoint[5] == 3;
    f.campaignGameOver();
    const bool failed = f.ui().state() == UIState::CampaignGameOver && f.ui().openMovie() == "UI_GFxGameOver_p.CampaignGameOver_GFX_1";
    f.call("Game.RestartPlayerFromLastCheckpoint", {});
    const bool asked = f.takeCampaignRestartRequest() && !f.takeCampaignRestartRequest();
    f.onUIEvent(5);   // the restart's respawn
    const bool restarted = f.ui().state() == UIState::InGame && f.ui().openMovie().empty();
    f.campaignMissionComplete(101, 1);
    LocalProfile back; back.load();
    const bool complete = back.campaign.complete[5] && back.campaign.available[6] && back.campaign.bestDifficulty[5] == 1 &&
                          back.campaign.checkpoint[5] == 3;
    const bool toLobby = runUntil(rt, [&] { if (f.wantsWorldUnload()) f.worldUnloaded(); return f.level() == LevelKind::GameLobby; }, 60 * 20);
    check(coop && inGame && notIndexed && indexed && failed && asked && restarted && complete && toLobby, "campaign.C6_runtime_events",
          std::to_string(coop) + std::to_string(inGame) + std::to_string(notIndexed) + std::to_string(indexed) + std::to_string(failed) +
          std::to_string(asked) + std::to_string(restarted) + std::to_string(complete) + std::to_string(toLobby) + " " + coopWhy);
    std::remove("wfc_profile.ini");
}

static void testLobbyFlowV2() {
    // LV1: Private Match hosts at once (offline: the same lobby with bots), with the profile's last mode (new profile: TDM)
    {
        FrontendRuntime rt;
        rt.init();
        GameFlow& f = rt.flow();
        bool ok = runUntil(rt, [&] { return f.frontEndStarted(); });
        f.call("Online.OpenPartyLobby", {"GTS_TeamGame"});
        ok = ok && runUntil(rt, [&] { return f.level() == LevelKind::PartyLobby && !f.loading().active; });
        f.profile().lobbyLastMode.clear();
        f.call("Online.HostPrivateNow");
        ok = ok && runUntil(rt, [&] { return f.level() == LevelKind::GameLobby && !f.loading().active; });
        const bool tdm = f.lobby().gameModeTag == "TDM";
        // LV2 rows: the mode's host options then the PC rows
        const auto rows = f.lobbySettingRows();
        bool hasTime = false, hasBots = false;
        for (const auto& r : rows) { hasTime = hasTime || r.store == "<TnGameSettings:TimeLimit>"; hasBots = hasBots || r.store == "<TnGameSettings:WfcBotsAutobot>"; }
        // LV3: team -> team in place (no travel), via the Game Settings mode row
        DataStores& ds = rt.dataStores();
        ds.call("WriteValue", {"<WfcLobby:GameMode>", "CTF"});
        const bool inPlace = f.lobby().gameModeTag == "CTF" && f.level() == LevelKind::GameLobby && !f.loading().active &&
                             f.profile().lobbyLastMode == "CTF" && ds.call("ReadValue", {"<WfcLobby:GameMode>"}).s == "CTF";
        // team -> Deathmatch re-creates the lobby (another lobby class)
        ds.call("WriteValue", {"<WfcLobby:GameMode>", "DM"});
        const bool recreated = runUntil(rt, [&] { return f.level() == LevelKind::GameLobby && !f.loading().active && f.lobby().gameModeTag == "DM"; });
        f.profile().lobbyLastMode.clear();
        f.profile().save();
        check(ok && tdm && hasTime && hasBots && inPlace && recreated, "lobby.LV1_LV3_host_now_and_mode_change",
              std::to_string(ok) + std::to_string(tdm) + std::to_string(hasTime) + std::to_string(hasBots) + std::to_string(inPlace) + std::to_string(recreated));
    }
    // LV7: Escalation is a Game Settings mode; the lobby service address reaches the session and the profile
    {
        OnlineRig r;
        r.frames(2);
        const bool sent = r.fake->count("setLobbyService") == 1;
        r.f().setLobbyService("lobby.example.net:27900");
        check(r.ok && sent && r.fake->last("setLobbyService")->arg == "lobby.example.net:27900" &&
              r.f().profile().lobbyService == "lobby.example.net:27900" &&
              std::find(GameFlow::lobbyModes().begin(), GameFlow::lobbyModes().end(), "SV") != GameFlow::lobbyModes().end(),
              "lobby.LV7_escalation_mode_and_service");
        r.f().profile().lobbyService.clear();
        r.f().profile().save();
    }
    // LV5: Quick Match (online: the playlist choice searches): progress, then joined; a second search finds none -> hosts
    {
        using T = SessionEvent::Type;
        SessionLobby l; l.gameModeTag = "TDM"; l.mapId = 508;
        SessionEvent prog = ev(T::SearchProgress); prog.searchTried = 2; prog.searchOf = 5;
        SessionEvent j = ev(T::Joined); j.lobby = l; j.member.fingerprint = "WFC-SELF-SELF-9";
        OnlineRig r({after("quickMatch", prog), after("quickMatch", j, 0.5)});
        r.f().call("Online.PlayPlaylist", {"1", "false"});
        const bool searching = r.fake->count("quickMatch") == 1 && r.f().popup().open &&
                               r.f().popup().title == r.rt->catalog().localizeKey("$UIText.MessagePrompts.SearchForGameTitle");
        r.frames(2);
        const bool reserving = r.f().popup().message.find("2") != std::string::npos && r.f().popup().message.find("5") != std::string::npos;
        const bool joined = runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; }) &&
                            r.f().sessionRole() == GameFlow::SessionRole::Member;
        SessionEvent ho = ev(T::HostOpened); ho.joinCode = "7QK2M-9TDAB";
        OnlineRig h({after("quickMatch", ho)});
        h.f().call("Online.PlayPlaylist", {"1", "false"});
        const bool hosted = runUntil(*h.rt, [&] { return h.f().level() == LevelKind::GameLobby && !h.f().loading().active; }) &&
                            h.f().sessionRole() == GameFlow::SessionRole::Host && h.f().lobby().playlistId == 1;
        check(r.ok && searching && reserving && joined && hosted, "lobby.LV5_quick_match",
              std::to_string(searching) + std::to_string(reserving) + std::to_string(joined) + std::to_string(hosted));
    }
    // LV6: the public lobby's map vote (ONLINE_LOBBY_FLOW_V2 4.1): host side - 3 options + None of these, tallies from the
    // members' info, the 45 s autostart at 4 humans, a None of these majority re-rolls (round 2, new maps, no None), the vote
    // closes at 10 s and the launch goes out; member side - the vote from LobbyState, a cast reaches setMemberInfo.
    {
        using T = SessionEvent::Type;
        SessionEvent ho = ev(T::HostOpened); ho.joinCode = "7QK2M-9TDAB";
        OnlineRig h({after("quickMatch", ho)});
        h.f().call("Online.PlayPlaylist", {"1", "false"});
        const bool hosted = runUntil(*h.rt, [&] { return h.f().level() == LevelKind::GameLobby && !h.f().loading().active; }) &&
                            h.f().sessionRole() == GameFlow::SessionRole::Host;
        const GameFlow::MapVote& v = h.f().mapVote();
        const bool opened = v.open && v.round == 1 && v.maps.size() == 3 && v.counts.size() == 4 && h.f().lobby().mapId == v.maps[0];
        const std::vector<int> round1 = v.maps;
        auto voter = [&](const char* fp, int vote) { SessionEvent e = memberEv(fp, "Voter", true); e.info.mapVote = vote; return e; };
        h.f().castMapVote(1);
        h.fake->inject(voter("WFC-AAAA-AAAA-1", 1));
        h.fake->inject(voter("WFC-BBBB-BBBB-2", 2));
        h.fake->inject(voter("WFC-CCCC-CCCC-3", 3));
        h.frames(3);
        const bool tallied = v.counts == std::vector<int>({0, 2, 1, 1}) && h.f().lobby().mapId == v.maps[1] && h.f().lobby().countingDown &&
                             h.f().lobby().countdown >= 44;
        const FakeSession::Call* pub = h.fake->last("publishLobby");
        const bool published = pub && pub->lobby.voteRound == 1 && pub->lobby.voteOpen && pub->lobby.voteMaps == round1 &&
                               pub->lobby.voteCounts == v.counts;
        // None of these: the host and two members -> 3 of 4 humans, a strict majority -> round 2
        h.f().castMapVote(3);
        h.fake->inject(voter("WFC-AAAA-AAAA-1", 3));
        h.fake->inject(voter("WFC-BBBB-BBBB-2", 3));
        h.frames(3);
        bool disjoint = true;
        for (int id : v.maps) disjoint = disjoint && std::find(round1.begin(), round1.end(), id) == round1.end();
        const bool rerolled = v.round == 2 && v.open && v.counts.size() == v.maps.size() && !v.maps.empty() && h.f().myMapVote() == -1 &&
                              (disjoint || v.maps.size() < 3);
        const bool closed = runUntil(*h.rt, [&] { return !h.f().mapVote().open; }, 3000) && h.f().lobby().countdown <= 10 &&
                            h.f().mapVote().winner == h.f().lobby().mapId &&
                            std::find(v.maps.begin(), v.maps.end(), h.f().mapVote().winner) != v.maps.end() && h.fake->count("setLaunch") == 1 &&
                            h.fake->last("setLateJoinLimit") && h.fake->last("setLateJoinLimit")->arg == "300";
        // member: the vote arrives with the LobbyState; a cast goes to the host in the member info
        SessionLobby l; l.gameModeTag = "TDM"; l.mapId = 508; l.voteMaps = {508, 501, 503}; l.voteCounts = {1, 0, 0, 0}; l.voteRound = 1; l.voteOpen = true;
        SessionEvent j = ev(T::Joined); j.lobby = l; j.member.fingerprint = "WFC-SELF-SELF-9";
        OnlineRig m({after("join", j)});
        m.f().call("Online.JoinGame", {"7QK2M-9TDAB"});
        const bool memberIn = runUntil(*m.rt, [&] { return m.f().level() == LevelKind::GameLobby && !m.f().loading().active; });
        SessionEvent ls = ev(T::LobbyState); ls.lobby = l;
        m.fake->inject(ls);
        m.frames(3);
        const bool cast = m.f().mapVote().round == 1 && m.f().castMapVote(2) && m.fake->last("setMemberInfo") && m.fake->last("setMemberInfo")->info.mapVote == 2;
        const bool memberStart = m.f().call("Online.BeginLobbyExitCountdown", {}).s.empty() && m.fake->count("requestStart") == 0;
        check(h.ok && hosted && opened && tallied && published && rerolled && closed && m.ok && memberIn && cast && memberStart, "lobby.LV6_map_vote",
              std::to_string(hosted) + std::to_string(opened) + std::to_string(tallied) + std::to_string(published) + std::to_string(rerolled) +
              std::to_string(closed) + std::to_string(memberIn) + std::to_string(cast) + std::to_string(memberStart));
    }
    // LV8: join in progress (public games, once the session supports it): MatchInProgress -> joinRunningMatch ->
    // CountdownStarted{lateJoin} -> straight to the match map (no lobby countdown); CatchUpProgress fills the loading text.
    // Without session support (today): D6, wait in the lobby.
    {
        using T = SessionEvent::Type;
        SessionLobby l; l.gameModeTag = "TDM"; l.mapId = 508; l.voteMaps = {508, 501, 503}; l.voteCounts = {0, 0, 0, 0}; l.voteRound = 1;
        l.matchRunning = true;
        SessionEvent mip = ev(T::MatchInProgress); mip.lobby = l; mip.member.fingerprint = "WFC-SELF-SELF-9";
        OnlineRig d6({after("join", mip)});
        d6.f().call("Online.JoinGame", {"7QK2M-9TDAB"});
        const bool waits = runUntil(*d6.rt, [&] { return d6.f().level() == LevelKind::GameLobby && !d6.f().loading().active; }) &&
                           d6.f().matchInProgressForMember() && d6.fake->count("joinRunningMatch") == 0;
        SessionEvent late = ev(T::CountdownStarted); late.lateJoin = true; late.seed = 42u; late.lateSeat = 1;
        late.launchUrl = "MP_IAC_Streets_Base_m?GameModeTag=TDM?GameTeamStatus=3?MapId=508?listen";
        SessionSlot hs; hs.kind = SessionSlot::Kind::Human; hs.member.fingerprint = "WFC-HOST-HOST-1"; hs.team = 0;
        SessionSlot me; me.kind = SessionSlot::Kind::Human; me.member.fingerprint = "WFC-SELF-SELF-9"; me.team = 1;
        late.lobby = l; late.lobby.slots = {hs, me};
        SessionEvent cu = ev(T::CatchUpProgress); cu.catchUpTick = 300; cu.catchUpTarget = 600;
        OnlineRig r({after("join", mip), after("joinRunningMatch", late), after("reportLoaded", cu, 0.2)});
        r.fake->lateJoin = true;
        r.f().call("Online.JoinGame", {"7QK2M-9TDAB"});
        const bool asked = runUntil(*r.rt, [&] { return r.fake->count("joinRunningMatch") == 1; });
        const bool loading = runUntil(*r.rt, [&] { return r.f().loading().active && r.f().loading().kind == "Map"; }) &&
                             !r.f().lobby().countingDown && r.f().currentMatch().seed == 42u && r.f().currentMatch().teamIndex == 1;
        r.f().matchLoaded();   // the Application loads the world; the replay (CatchUpProgress) runs under the load gate
        const bool progress = runUntil(*r.rt, [&] { return r.f().loading().message.find("50") != std::string::npos; });
        r.f().characterSelected();   // the late joiner picks: the match has begun - no PreGameCountdown, it spawns at once
        const bool begun = r.f().ui().openMovie() != "UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1";
        // a refusal (the match is past the catch-up limit): the lobby wait stays, with the reason
        SessionEvent tfa = ev(T::JoinFailed); tfa.joinFailure = SessionJoinFailure::TooFarAlong;
        OnlineRig x({after("join", mip), after("joinRunningMatch", tfa)});
        x.fake->lateJoin = true;
        x.f().call("Online.JoinGame", {"7QK2M-9TDAB"});
        const bool refused = runUntil(*x.rt, [&] { return x.popupIs("$WfcPC.Online.LateJoinTitle", "$WfcPC.Online.LateJoinTooLate"); }) &&
                             x.f().level() == LevelKind::GameLobby && x.f().matchInProgressForMember();
        check(d6.ok && waits && r.ok && asked && loading && progress && begun && x.ok && refused, "lobby.LV8_join_in_progress",
              std::to_string(waits) + std::to_string(asked) + std::to_string(loading) + std::to_string(progress) + std::to_string(begun) + std::to_string(refused));
    }
    // LV4: a member cannot change the mode
    {
        OnlineRig r;
        const bool in = r.ok && memberIn(r, false);
        r.rt->dataStores().call("WriteValue", {"<WfcLobby:GameMode>", "CTF"});
        check(in && r.f().lobby().gameModeTag == "TDM" && !r.f().changeLobbyMode("CTF"), "lobby.LV4_member_no_settings");
    }
}

static void testForfeitEnd() {
    // A team with no players left forfeits (Gameplay's 1.5 s check, Systems 10.5): the winners' end screen shows the
    // original "Enemy team forfeits"; any other end keeps Gameplay's line; the flow returns to the game lobby as usual.
    SessionEvent o = ev(SessionEvent::Type::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
    OnlineRig r({after("host", o)});
    const bool host = r.ok && hostLobby(r);
    DataStores& ds = r.rt->dataStores();
    MatchValues v; v.valid = true; v.myTeam = 0; v.winnerTeam = 0; v.endReason = "Forfeit"; v.gameOverMessage = "Your team won";
    r.f().setMatchValues(v);
    ds.poll();
    const std::string forfeit = ds.call("ReadValue", {"<CurrentGame:GameOverMessage>"}).s;
    v.endReason = "Score";
    r.f().setMatchValues(v);
    ds.poll();
    const std::string score = ds.call("ReadValue", {"<CurrentGame:GameOverMessage>"}).s;
    check(host && forfeit == "Enemy team forfeits" && score == "Your team won" &&
          !r.rt->catalog().localizeKey("$WfcPC.Online.BotsLeaverDesc").empty(), "online.F1_forfeit_result_text", forfeit + " / " + score);
}

static void testLocalizeCaseInsensitive() {
    FrontendRuntime rt;
    rt.init();
    check(rt.catalog().localizeKey("$UITEXT.CHARACTERSELECT.WEAPON2") == rt.catalog().localizeKey("$UIText.CharacterSelect.Weapon2") &&
          !rt.catalog().localizeKey("$UIText.CharacterSelect.Weapon2").empty() && rt.catalog().localizeKey("$UITEXT.NO.SUCHKEY").empty(),
          "catalog.localize_key_case_insensitive");
}

static void testOnlineFirstRun() {
    const std::string title = [] { FrontendRuntime t; t.init(); return t.catalog().localizeKey("$WfcPC.FirstRun.Title"); }();
    {   // FR1: online, a fresh profile -> the prompt (no account: "Choose the name..."); offline -> never
        FirstRunRig r;
        const bool shown = r.ok && r.f().popup().open && r.f().popup().title == title &&
                           r.f().popup().message == r.rt->catalog().localizeKey("$WfcPC.FirstRun.ChooseName");
        std::remove("wfc_profile.ini");
        FrontendRuntime off;
        bool offline = off.init();
        GameFlow& f = off.flow();
        offline = offline && runUntil(off, [&] { return f.frontEndStarted(); });
        f.call("Online.OpenPartyLobby", {"GTS_TeamGame"});
        offline = offline && runUntil(off, [&] { return f.level() == LevelKind::PartyLobby && !f.loading().active; });
        for (int i = 0; i < 5; ++i) off.update(platform::InputFrame{}, 1.0f / 60.0f);
        check(shown && offline && !f.popup().open && !f.profile().onlineFirstRunShown, "online.FR1_prompt_online_only");
    }
    {   // FR2 / FR3: Choose Name -> the account prompt (presenter) -> CreateAccount + Login -> the prompt again with the name and
        // the voice line; Voice Settings -> answered for good, the Audio page requested; never again for this profile
        FirstRunRig r;
        r.f().popupButton('A');   // Choose Name
        const bool nameReq = r.f().takeFirstRunUi() == GameFlow::FirstRunUi::NamePrompt && !r.f().profile().onlineFirstRunShown;
        r.rt->bridge("UI_GFxLobbies_p.PartyLobby_GFX_1", "Account.CreateAccount", {"Name2"});
        r.rt->bridge("UI_GFxLobbies_p.PartyLobby_GFX_1", "Account.Login", {"Name2"});
        r.f().firstRunNameEntered();
        r.f().popupButton('A');   // the original CreateAccount result box
        r.frames(3);
        const std::string msg = r.f().popup().message;
        const bool again = r.f().popup().open && r.f().popup().title == title && msg.find("Name2") != std::string::npos &&
                           msg.find("Push to Talk (V)") != std::string::npos;
        check(r.ok && nameReq && again, "online.FR2_name_then_prompt_again", msg);
        r.f().popupButton('Y');   // Voice Settings
        const bool voice = r.f().takeFirstRunUi() == GameFlow::FirstRunUi::VoiceSettings && r.f().profile().onlineFirstRunShown;
        std::ifstream in("wfc_profile.ini"); std::stringstream ss; ss << in.rdbuf(); in.close();
        r.f().call("Online.OpenPartyLobby", {"GTS_TeamGame"});   // a new visit
        runUntil(*r.rt, [&] { return r.f().level() == LevelKind::PartyLobby && !r.f().loading().active; });
        r.frames(3);
        check(voice && ss.str().find("[Online]\nFirstRunShown=1") != std::string::npos && !r.f().popup().open, "online.FR3_voice_settings_answers");
    }
    {   // FR4: no name, Back -> not answered; the next visit asks again; Multiplayer stays usable
        FirstRunRig r;
        r.f().popupButton('B');
        r.frames(3);
        const bool closed = !r.f().popup().open && !r.f().profile().onlineFirstRunShown;
        r.f().call("Online.OpenPartyLobby", {"GTS_TeamGame"});
        runUntil(*r.rt, [&] { return r.f().level() == LevelKind::PartyLobby && !r.f().loading().active; });
        r.frames(3);
        check(r.ok && closed && r.f().popup().open && r.f().popup().title == title, "online.FR4_back_asks_next_visit");
    }
    std::remove("wfc_profile.ini");
}

static void testLaunchRecordAndIdleKick() {
    using T = SessionEvent::Type;
    // IK1: "Kick Idle Players" - online host row, Off by default (no URL option); On -> ?WfcIdleKick=1 in the launch URL
    SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD-4H"; o.address = "192.168.1.20:7777";
    OnlineRig r({after("host", o)});
    const bool host = r.ok && hostLobby(r);
    DataStores& ds = r.rt->dataStores();
    const std::string fields = ds.call("GetDataStoreFields", {"TnGameSettings"}).s;
    const bool off = fields.find("WfcIdleKick") != std::string::npos && ds.call("ReadValue", {"<TnGameSettings:WfcIdleKick>"}).s == "Off" &&
                     r.f().launchUrlForTest().find("WfcIdleKick") == std::string::npos;
    ds.call("WriteValue", {"<TnGameSettings:WfcIdleKick>", "On"});
    const bool on = r.f().launchUrlForTest().find("?WfcIdleKick=1") != std::string::npos;
    check(host && off && on, "online.IK1_idle_kick_row", fields);
    // L1: the host hands the launch record (its full match URL + a seed) to the session before Start
    r.f().call("Online.BeginLobbyExitCountdown");
    const FakeSession::Call* lc = r.fake->last("setLaunch");
    check(lc && lc->arg.rfind(r.f().launchUrlForTest() + "|", 0) == 0 && r.fake->count("requestStart") == 1, "online.L1_host_sends_launch_record",
          lc ? lc->arg : std::string());
    r.f().profile().bots.idleKick = false;
    r.f().profile().save();
    // L2: a member launches from the host's record verbatim (not its own profile rows)
    OnlineRig m;
    const bool in = m.ok && memberIn(m, false);
    const std::string hostUrl = "MP_IAC_Streets_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame"
                                "?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters="
                                "?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=508"
                                "?BotsAutobot=3?BotsDecepticon=4?BotsFriendly=3?BotsEnemy=4?BotDifficulty=2?ExtendedPlayers=0?WfcIdleKick=1";
    SessionEvent cs = ev(T::CountdownStarted); cs.launchUrl = hostUrl; cs.seed = 4242;
    m.fake->inject(cs);
    const bool launched = runUntil(*m.rt, [&] { return m.f().hasPendingMatch(); }, 60 * 30);
    check(in && launched && m.f().currentMatch().url.toString() == hostUrl && m.f().currentMatch().seed == 4242, "online.L2_member_launches_host_record",
          m.f().currentMatch().url.toString());
}

int main() {
    std::string vs = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    Catalog c;
    bool ok = c.load(Catalog::defaultManifestRoot(), Catalog::defaultExtractedRoot(), vs + "/Maps");
    check(ok, "catalog.load");
    testUrl();
    testProfileBotMigration();
    testRecommendedBots();
    testGraphicsAutoDetect();
    testHdTexturesBoot();
    testPackageCredits();
    testFpsCounter();
    testFakeSession();
    testCampaignStrings();
    testCampaignObjectives();
    testCampaignTutorials();
    testCampaignCinematicHud();
    testOnlineBoundary();
    testOnlineHostJoin();
    testOnlineLobbyGate();
    testOnlineMemberSlots();
    testHudJamBuffs();
    testOnlineStartLoadLeave();
    testOnlineMemberCountdown();
    testOnlineSessionEnd();
    testOnlineVoice();
    testOnlineMatchSide();
    testIconicMode();
    testVoiceUi();
    testOnlineFirstRun();
    testLaunchRecordAndIdleKick();
    testLocalizeCaseInsensitive();
    testForfeitEnd();
    testMemberSeatOrder();
    testLobbyWaitingOne();
    testLensFlaresSetting();
    testLobbyFlowV2();
    testRespawnUiLifetime();
    testCampaignC1C4();
    testCampaignC3();
    testCampaignC2C5();
    testSmokeWaterRows();
    testCampaignC6();
    testEscalationNoXp();
    testCampaignObjectivesManifest();
    testVolumetricSetting();
    if (ok) testCatalog(c);
    if (ok) testHudObservers(c);
    if (ok) testInputPrompts(c);
    testUIController();
    testSceneCamera();
    if (ok) testFlow();
    if (ok) testCatalogExtensibility(Catalog::defaultManifestRoot(), Catalog::defaultExtractedRoot());
    std::printf("frontend tests: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail;
}
