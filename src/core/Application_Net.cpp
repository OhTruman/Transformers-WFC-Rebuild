// [Systems] Online match over the lockstep layer (ONLINE_MP_ARCHITECTURE.md, NETCODE_HARNESS.md section 2) - the headless form the
// netcode harness runs (two or more wfc_rebuild processes, null render, each its own human seat driven by its PlayerBot through the
// command stream). Switches:
//   WFC_NET_SLOT=n            this process's seat (0 = host)          WFC_NET_SEATS=N     humans (default 2)
//   WFC_NET_BIND=host:port    this process's UDP socket              WFC_NET_PEERS=s=host:port[,...] where each seat's packets go
//                                                                    (a plain list host:port,... = the other seats in ascending order)
//   WFC_NET_DELAY=D (auto)    WFC_NET_HASH_EVERY=k (60)              WFC_NET_LOG=path (per-tick CSV)
//   WFC_NET_SEED=s            the match seed (the launch record's; default 0x5eed0001)
//   WFC_NET_TIMEOUT_S=t (10)  WFC_NET_DESYNC_AT=tick (TEST ONLY: this seat reports a flipped state hash at that tick)
//   WFC_NET_PACE=1            real-time pacing: tick N steps no earlier than N x 1/60 s after the start (a real match's rhythm;
//                             the impairment profiles' timed bursts / outages); unset = as fast as the commands arrive
//   WFC_NET_RECORD=file       this peer writes every applied tick's commands (all seats, wire bytes) - the replay input
//   WFC_NET_REPLAY=file       no network: one process runs the same launch record fed a recorded file's commands and writes the
//                             same per-tick WFC_NET_LOG CSV (replay equivalence: its simhash stream must equal the live peers')
// Exit codes: 0 the match ended / the run's end point, 3 desync, 4 peer timeout, 5 host lost, 6 launch mismatch, 7 transport error,
// 8 replay file error / end of the recording.
//   WFC_NET_VOICE=tone|mic    the voice lane: tone = every seat talks a synthetic 440 Hz tone all match (TEST ONLY, no device);
//                             mic = the default microphone, open while the match runs (only with the user's OK). Headless runs have
//                             no audio device: frames are received, buffered and decoded, the stats logged at the end.
#include "core/Application.h"
#include "core/Log.h"
#include "game/World.h"
#include "net/Lockstep.h"
#include "net/NetTransport.h"
#include "net/UdpSession.h"
#include "net/NetMatchDriver.h"
#include "frontend/FrontendRuntime.h"
#include "net/VoiceCodec.h"
#include "net/VoiceFrames.h"
#include "net/VoiceSession.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
std::vector<std::pair<int, std::string>> parsePeers(const char* s, int self, int seats) {
    std::vector<std::pair<int, std::string>> out;
    if (!s) return out;
    std::string all = s;
    int nextImplicit = 0;
    for (size_t at = 0; at <= all.size();) {
        size_t e = all.find(',', at); if (e == std::string::npos) e = all.size();
        std::string item = all.substr(at, e - at); at = e + 1;
        if (item.empty()) continue;
        const size_t eq = item.find('=');
        if (eq != std::string::npos) { out.push_back({std::atoi(item.substr(0, eq).c_str()), item.substr(eq + 1)}); continue; }
        while (nextImplicit == self) ++nextImplicit;
        if (nextImplicit < seats) out.push_back({nextImplicit++, item});
    }
    return out;
}
uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const unsigned char* c = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ULL; }
    return h;
}
}  // namespace

namespace core {

bool Application::netMatchRequested() { return std::getenv("WFC_NET_SLOT") || std::getenv("WFC_UDPHOST") || std::getenv("WFC_UDPJOIN"); }

void Application::runNetMatch() {
    if (std::getenv("WFC_UDPHOST") || std::getenv("WFC_UDPJOIN")) { runUdpSessionMatch(); return; }
    auto env = [](const char* k, long d) { const char* v = std::getenv(k); return v ? std::atol(v) : d; };
    const int self = (int)env("WFC_NET_SLOT", 0), seats = (int)env("WFC_NET_SEATS", 2);
    const char* bind = std::getenv("WFC_NET_BIND");
    const char* url = std::getenv("WFC_MATCH_URL");
    const char* replay = std::getenv("WFC_NET_REPLAY");
    if ((!bind && !replay) || !url || seats < 2 || self < 0 || self >= seats) {
        LOG_ERROR("NET: WFC_NET_SLOT needs WFC_NET_BIND (or WFC_NET_REPLAY), WFC_MATCH_URL and 0 <= slot < WFC_NET_SEATS (>= 2)");
        exitCode_ = 7; return;
    }

    // The launch record (ONLINE_MP_ARCHITECTURE 10.3): the URL, the seed and the seat table, identical on every peer; its hash is the
    // HELLO check. MatchLaunch::humans lists every human in seat order (this machine's included, at localSeat), so match player i is
    // human i on every peer (Gameplay GAMEPLAY_SEAT_ORDER): wire seat index == match player.
    game::MatchLaunch L;
    if (!game::MatchLaunch::fromURL(url, L)) { LOG_ERROR("NET: bad WFC_MATCH_URL"); exitCode_ = 7; return; }
    L.seed = (uint32_t)env("WFC_NET_SEED", 0x5eed0001);
    if (L.seed == 0) L.seed = 1;   // 0 = offline
    L.localTeam = self % 2;
    game::CharacterSelection cs; cs.type = 0; cs.specialty = game::Specialty::Soldier; cs.weapons = {"AssaultRifle", "HomingRocket", "FlakGrenades"};
    for (int s = 0; s < seats; ++s) {
        game::RemoteSeat r; r.name = "Seat" + std::to_string(s); r.team = s % 2; r.member = "SEAT" + std::to_string(s);
        r.selected = true; r.selection = cs;
        L.humans.push_back(r);
    }
    L.localSeat = self;
    L.online = true;
    uint64_t lh = 1469598103934665603ULL;
    lh = fnv(lh, url, std::strlen(url)); lh = fnv(lh, &L.seed, sizeof L.seed); lh = fnv(lh, &seats, sizeof seats);
    const char* build = __DATE__ " " __TIME__;   // the same exe on every peer (a different build = a different sim)
    lh = fnv(lh, build, std::strlen(build));
    world_.launchMatch(L);
    // humans[localSeat].selection is applied by launchMatch (GAMEPLAY_NET_IDENTITY): no local selectCharacter here
    std::vector<int> seatMp((size_t)seats, -1);   // wire seat -> match player (== the seat with the seat order; checked here)
    for (int s = 0; s < seats; ++s) seatMp[(size_t)s] = s;
    if (world_.localMatchPlayer() != self) {
        LOG_ERROR("NET: the local seat %d is match player %d - the launch did not apply the seat order", self, world_.localMatchPlayer());
        exitCode_ = 6; return;
    }
    std::vector<int> order(seats);   // apply order: ascending match player
    for (int s = 0; s < seats; ++s) order[(size_t)s] = s;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return seatMp[(size_t)a] < seatMp[(size_t)b]; });
    for (int s = 0; s < seats; ++s) LOG_INFO("NET seat %d -> match player %d%s", s, seatMp[(size_t)s], s == self ? " (local)" : "");
    const float step = clock_.stepSeconds();
    const float matchSeconds = (float)std::atof(std::getenv("WFC_MATCH_SECONDS") ? std::getenv("WFC_MATCH_SECONDS") : "0");

    // Record / replay file: "WFCR" u32 version 1, u64 launch hash, u32 seats; then per applied tick: u32 tick + seats x 23 wire bytes.
    if (replay) {
        FILE* f = std::fopen(replay, "rb");
        char magic[4] = {0}; uint32_t ver = 0, rseats = 0; uint64_t rhash = 0;
        if (!f || std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "WFCR", 4) != 0 || std::fread(&ver, 4, 1, f) != 1 ||
            std::fread(&rhash, 8, 1, f) != 1 || std::fread(&rseats, 4, 1, f) != 1 || ver != 1 || (int)rseats != seats) {
            LOG_ERROR("NET replay: %s is not a %d-seat WFC_NET_RECORD file", replay, seats);
            if (f) std::fclose(f);
            exitCode_ = 8; return;
        }
        if (rhash != lh) LOG_WARN("NET replay: the recording's launch hash %016llx differs from this launch's %016llx (another URL / seed / build)",
                                  (unsigned long long)rhash, (unsigned long long)lh);
        FILE* log = nullptr;
        if (const char* lp = std::getenv("WFC_NET_LOG")) {
            log = std::fopen(lp, "w");
            if (log) std::fprintf(log, "tick,simhash,bundlehash,stall_ms,bytes_in,bytes_out,rtt_ms\n");
        }
        long steps = 0;
        std::vector<net::WireCommand> rec((size_t)seats);
        for (;;) {
            uint32_t tick = 0;
            if (std::fread(&tick, 4, 1, f) != 1) { LOG_INFO("NET replay: end of the recording after %ld ticks", steps); break; }
            bool ok = true;
            for (auto& w : rec) ok = ok && std::fread(w.data(), 1, w.size(), f) == w.size();
            if (!ok || tick != (uint32_t)world_.simTick() + 1) { LOG_ERROR("NET replay: tick %u out of sequence (sim at %lld)", tick, (long long)world_.simTick()); exitCode_ = 8; break; }
            uint64_t bh = 1469598103934665603ULL;
            for (int s : order) {
                game::RemoteCommand c;
                net::decodeCommand(rec[(size_t)s], tick, c);
                world_.applyRemoteCommand(seatMp[(size_t)s], c);
            }
            for (const auto& w : rec) bh = fnv(bh, w.data(), w.size());
            world_.tick(step);
            gameMode_.tick(world_, step);
            ++steps;
            if (log) std::fprintf(log, "%u,%016llx,%016llx,0.00,0,0,0.0\n", tick, (unsigned long long)world_.simStateHash(), (unsigned long long)bh);
        }
        if (log) std::fclose(log);
        std::fclose(f);
        LOG_INFO("NET replay done: %ld ticks from %s", steps, replay);
        if (exitCode_ < 0) exitCode_ = 0;
        return;
    }

    std::string err;
    std::unique_ptr<net::INetTransport> t = net::createUdpTransport(self, bind, parsePeers(std::getenv("WFC_NET_PEERS"), self, seats), err);
    if (!t) { LOG_ERROR("NET transport: %s", err.c_str()); exitCode_ = 7; return; }
    FILE* recFile = nullptr;
    if (const char* rp = std::getenv("WFC_NET_RECORD")) {
        recFile = std::fopen(rp, "wb");
        if (recFile) { const uint32_t ver = 1, ns = (uint32_t)seats; std::fwrite("WFCR", 1, 4, recFile); std::fwrite(&ver, 4, 1, recFile);
                       std::fwrite(&lh, 8, 1, recFile); std::fwrite(&ns, 4, 1, recFile); }
        else LOG_WARN("NET: cannot write the recording %s", rp);
    }

    net::Lockstep::Config cfg;
    // Join in progress (harness): WFC_NET_LATE=<seat>[,<seat>] reserves seats on every peer (a human in the launch whose pawn idles
    // on empty commands until it joins - the sim side of a dormant seat is Gameplay's); the joiner runs with WFC_NET_LATEJOIN=1.
    if (const char* late = std::getenv("WFC_NET_LATE")) {
        cfg.activeFrom.assign((size_t)seats, 1);
        for (const char* q = late; *q;) {
            const int ls = std::atoi(q);
            if (ls > 0 && ls < seats) cfg.activeFrom[(size_t)ls] = 0;
            while (*q && *q != ',') ++q;
            if (*q == ',') ++q;
        }
        cfg.keepHistory = self == 0;
        cfg.lateJoin = std::getenv("WFC_NET_LATEJOIN") != nullptr;
    }
    cfg.seats = seats; cfg.self = self; cfg.delay = (int)env("WFC_NET_DELAY", 0); cfg.hashEvery = (int)env("WFC_NET_HASH_EVERY", 60);   // delay 0 = auto from the RTT
    cfg.peerTimeoutS = (double)env("WFC_NET_TIMEOUT_S", 10); cfg.launchHash = lh;
    if (const char* lp = std::getenv("WFC_NET_LOG")) cfg.logPath = lp;
    net::Lockstep ls(*t, cfg);
    if (cfg.keepHistory) ls.setJoinReady([&ls](int seat) { ls.activateSeat(seat, seat % 2); });   // the harness: the launch's team
    const long desyncAt = env("WFC_NET_DESYNC_AT", -1);
    std::unique_ptr<net::VoiceSession> voice;
    if (const char* vm = std::getenv("WFC_NET_VOICE")) {
        voice = std::make_unique<net::VoiceSession>(ls, audio_, net::VoiceSession::Config{});
        if (std::strcmp(vm, "mic") == 0) voice->setSource(net::createVoiceCapture());
        else voice->setSyntheticTone(440.0f);
    }
    auto seatPos = [this, &seatMp](int seat, core::Vec3& out) {
        const game::Character* c = world_.participantPawn(seatMp[(size_t)seat]);
        if (!c) return false;
        out = c->position() + core::Vec3{0, 1.8f, 0};   // the head (approx.)
        return true;
    };
    // Occlusion: the world line check the cue mix uses (World::soundBlocked); the room: the listener's current SeqAct_Reverb preset.
    auto occludedQ = [this](const core::Vec3& from, const core::Vec3& to) { return world_.soundBlocked(from, to); };
    auto roomQ = [this]() { return world_.currentReverbPreset(); };
    // The host's voice fan-out audience (star relay): radio = the talker's team, proximity = the listener seat within earshot of the
    // talker by the sim's pawn positions (+4 m margin for the frames in flight). Every peer could compute it; the host does.
    ls.setVoiceAudience([this, &seatMp](int talker, int listener, uint8_t flags) {
        const auto& ps = world_.match().players();
        const int a = seatMp[(size_t)talker], b = seatMp[(size_t)listener];
        if (a < 0 || b < 0 || (size_t)a >= ps.size() || (size_t)b >= ps.size()) return false;
        if ((flags & 1) && ps[(size_t)a].team == ps[(size_t)b].team && ps[(size_t)a].team != 255) return true;   // radio
        if (!(flags & 2)) return false;
        const game::Character* x = world_.participantPawn(a);
        const game::Character* y = world_.participantPawn(b);
        static const float maxD = std::getenv("WFC_VOICE_MAXDIST") ? (float)std::atof(std::getenv("WFC_VOICE_MAXDIST")) : 64.0f;
        return x && y && core::length(x->position() - y->position()) < maxD + 4.0f;
    });
    auto sameTeam = [this, &seatMp](int seat) {
        const auto& ps = world_.match().players();
        const int a = seatMp[(size_t)seat], b = world_.localMatchPlayer();
        return a >= 0 && b >= 0 && (size_t)a < ps.size() && (size_t)b < ps.size() && ps[(size_t)a].team == ps[(size_t)b].team && ps[(size_t)a].team != 255;
    };

    // Wait for every peer's HELLO (the load gate: no tick before everyone is here).
    const auto t0 = std::chrono::steady_clock::now();
    auto secsSince = [](std::chrono::steady_clock::time_point a) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count(); };
    while (!ls.everyoneHere() && ls.status() == net::Lockstep::Status::Running) {
        ls.pump();
        if (secsSince(t0) > cfg.peerTimeoutS * 3.0) { LOG_ERROR("NET: peers did not arrive in %.0f s", cfg.peerTimeoutS * 3.0); exitCode_ = 4; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    LOG_INFO("NET all %d seats here after %.2f s", seats, secsSince(t0));

    uint32_t nextLocal = 1;
    const bool pace = std::getenv("WFC_NET_PACE") != nullptr;
    long steps = 0;
    bool started = false;
    auto tRun = std::chrono::steady_clock::now();
    while (ls.status() == net::Lockstep::Status::Running) {
        ls.pump();
        for (net::Lockstep::Event ev; ls.pollEvent(ev);)   // the harness has no event consumers; a seat join is logged by the Lockstep
            (void)ev;
        if (voice) {   // the listener: the local pawn (its right vector from the facing yaw)
            const game::Character* me = world_.participantPawn(world_.localMatchPlayer());
            const core::Vec3 lp = me ? me->position() + core::Vec3{0, 1.8f, 0} : core::Vec3{0, 0, 0};
            const float yaw = me ? me->yaw() : 0.0f;
            const core::Vec3 right = core::normalize(core::cross(core::forwardFromYawPitch(yaw, 0.0f), core::Vec3{0, 1, 0}));
            voice->update(true, seatPos, sameTeam, occludedQ, roomQ, lp, right);
        }
        const uint32_t n = (uint32_t)world_.simTick() + 1;
        // This seat's commands up to N + D (input delay): the PlayerBot / controller input of now, encoded on the wire grid.
        while (nextLocal <= n + (uint32_t)ls.delay()) {
            platform::InputFrame input;
            world_.playerBotInput(input, step);
            world_.handleInput(input, step);
            ls.submitLocal(nextLocal, world_.encodeLocalCommand());
            ++nextLocal;
        }
        bool live = true;   // the joiner: full speed until it reaches its activeTick (caught up), then the match's real-time pace
        if (cfg.lateJoin) {
            static bool wasLive = false;
            ls.reportCatchUp(n);
            live = ls.activeFrom(self) != 0 && n >= ls.activeFrom(self);
            if (live && !wasLive) {
                LOG_INFO("NET late join: live at tick %u (active from %u, host at %u, %.1f s after start)", n, ls.activeFrom(self), ls.hostLive(), secsSince(tRun));
                tRun = std::chrono::steady_clock::now() - std::chrono::microseconds((long long)((double)n * step * 1e6));
            }
            wasLive = live;
        }
        if (pace && live && secsSince(tRun) < (double)n * step) {   // not yet this tick's wall time (real-time pacing)
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }
        if (ls.endTick() > 0 && n > ls.endTick()) { LOG_INFO("NET end: a peer finished at tick %u", ls.endTick()); break; }
        if (!ls.haveAll(n)) {
            const auto w0 = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            ls.stalled(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count());
            continue;
        }
        for (int s : order) {
            game::RemoteCommand c;
            ls.command(s, n, c);
            world_.applyRemoteCommand(seatMp[(size_t)s], c);
        }
        if (recFile) {   // the applied bundle, seat order (WFC_NET_RECORD)
            std::fwrite(&n, 4, 1, recFile);
            for (int s = 0; s < seats; ++s) { net::WireCommand w{}; ls.wire(s, n, w); std::fwrite(w.data(), 1, w.size(), recFile); }
        }
        world_.tick(step);
        gameMode_.tick(world_, step);
        ++steps;
        uint64_t h = world_.simStateHash();
        ls.stepped(n, h);
        if ((long)n == desyncAt) h ^= 1;   // TEST ONLY: the detector must name this tick
        ls.reportHash(n, h);
        if (n > 64) ls.forget(n - 64);
        const bool inProgress = world_.matchActive() && world_.match().state() == game::Match::State::InProgress;
        if (inProgress) started = true;
        if (started && !inProgress) { LOG_INFO("NET end: the match left InProgress at tick %u", n); break; }
        if (matchSeconds > 0.0f && inProgress && world_.match().elapsedTime() >= matchSeconds) {
            LOG_INFO("WFC_MATCH_SECONDS: %d s of match time reached at tick %u", (int)world_.match().elapsedTime(), n);
            break;
        }
        if ((float)steps * step > 900.0f) { LOG_INFO("NET end: 900 s simulated (guard)"); break; }
    }
    if (recFile) std::fclose(recFile);
    if (voice && self == 0) LOG_INFO("NET voice relay: %ld frames forwarded to other seats", ls.voiceRelayed());
    const int st = (int)ls.status();
    if (st == 0) ls.bye((uint32_t)world_.simTick());
    LOG_INFO("NET done: %ld steps in %.1f s wall, status %d %s", steps, secsSince(tRun), st, ls.statusText().c_str());
    exitCode_ = st;
}

// WFC_VOICEFRAMETEST (synthetic, no device): 2 s of a 440 Hz tone at amplitude 0.5 through net::VoiceFramer from 48 kHz stereo and 44.1 kHz
// mono, pushed in odd-sized device chunks. Expected per case: 100 frames of 320 samples, RMS level 0.354 (0.5 / sqrt 2), 440 Hz by zero
// crossings, and the frame count / level the Settings > Audio meter and the voice-activity gate read.
void Application::runVoiceFrameTest() {
    int fails = 0;
    struct Case { int rate, channels; };
    for (const Case cs : {Case{48000, 2}, Case{44100, 1}, Case{16000, 1}}) {
        net::VoiceFramer f;
        f.configure(cs.rate, cs.channels);
        const int total = cs.rate * 2;
        std::vector<float> buf;
        int frames = 0, crossings = 0; int16_t prev = 0; bool first = true;
        for (int at = 0; at < total;) {
            const int chunk = std::min(total - at, 441 + (at % 7) * 13);   // irregular device packets
            buf.assign((size_t)chunk * (size_t)cs.channels, 0.0f);
            for (int i = 0; i < chunk; ++i)
                for (int c = 0; c < cs.channels; ++c) buf[(size_t)i * (size_t)cs.channels + (size_t)c] = 0.5f * std::sin(6.2831853f * 440.0f * (float)(at + i) / (float)cs.rate);
            f.push(buf.data(), (size_t)chunk);
            at += chunk;
            net::VoiceFrame fr;
            while (f.pop(fr)) {
                ++frames;
                for (int16_t v : fr) { if (!first && ((prev < 0) != (v < 0))) ++crossings; prev = v; first = false; }
            }
        }
        const float hz = (float)crossings / 2.0f / 2.0f;   // 2 crossings per cycle, over 2 s
        const bool ok = frames == 100 && std::fabs(f.level() - 0.3536f) < 0.01f && std::fabs(hz - 440.0f) < 3.0f;
        if (!ok) ++fails;
        LOG_INFO("VOICEFRAMETEST %s %d Hz x %d ch: %d frames (want 100), level %.4f (want 0.354), tone %.1f Hz (want 440)", ok ? "PASS" : "FAIL",
                 cs.rate, cs.channels, frames, f.level(), hz);
    }
    // The codec round trip (WFC_OPUS builds): 1 s of the 440 Hz tone, frame sizes, decoded level, one concealed (lost) frame.
    if (net::voiceCodecAvailable()) {
        net::VoiceEncoder enc; net::VoiceDecoder dec;
        size_t bytes = 0; int frames = 0, decoded = 0; double sq = 0.0; long n = 0;
        for (int k = 0; k < 50; ++k) {
            net::VoiceFrame pcm;
            for (int i = 0; i < net::kVoiceFrameSamples; ++i)
                pcm[(size_t)i] = (int16_t)std::lround(0.5f * 32767.0f * std::sin(6.2831853f * 440.0f * (float)(k * net::kVoiceFrameSamples + i) / 16000.0f));
            std::vector<uint8_t> pkt;
            if (!enc.encode(pcm, pkt)) continue;
            ++frames; bytes += pkt.size();
            net::VoiceFrame out;
            const bool ok = k == 30 ? dec.decode(nullptr, 0, out) : dec.decode(pkt.data(), pkt.size(), out);   // frame 30: "lost"
            if (!ok) continue;
            ++decoded;
            if (k >= 10) for (int16_t v : out) { sq += ((double)v / 32768.0) * ((double)v / 32768.0); ++n; }
        }
        const double rms = n ? std::sqrt(sq / (double)n) : 0.0;
        const bool ok = frames == 50 && decoded == 50 && bytes / 50 <= 48 && rms > 0.25 && rms < 0.45;
        if (!ok) ++fails;
        LOG_INFO("VOICEFRAMETEST %s opus: %d frames, %.1f bytes / frame (16 kbit/s = 40), %d decoded incl. 1 concealed, decoded level %.3f (want ~0.354)",
                 ok ? "PASS" : "FAIL", frames, frames ? (double)bytes / frames : 0.0, decoded, rms);
    } else {
        LOG_INFO("VOICEFRAMETEST opus: not in this build (voice unavailable)");
    }
    LOG_INFO("VOICEFRAMETEST SUMMARY: %s", fails ? "FAIL" : "PASS");
    exitCode_ = fails ? 1 : 0;
}

// Gameplay's World::queueSeatSelection(seat, sel, atTick) (the in-match Choose Character applied at the same tick on every peer) when
// the tree has it; before that merge, the immediate select (NOT tick-synchronised: a warning, the match may desync on a repick).
template <class W, class = void> struct HasQueueSeatSelection : std::false_type {};
template <class W> struct HasQueueSeatSelection<W, std::void_t<decltype(std::declval<W&>().queueSeatSelection(0, std::declval<const game::CharacterSelection&>(), 0u))>> : std::true_type {};
template <class W> void queueSelection(W& w, int seat, const game::CharacterSelection& sel, uint32_t atTick) {
    if constexpr (HasQueueSeatSelection<W>::value) w.queueSeatSelection(seat, sel, atTick);
    else {
        LOG_WARN("online: World::queueSeatSelection missing in this build - seat %d's pick applied at once (tick %u), not tick-synchronised", seat, atTick);
        w.match().selectCharacter(seat, sel);
    }
}

// Join in progress (Gameplay's dormant seats): World::queueLateSeat / smallerTeam / seatPlayer when the tree has them. Before
// Gameplay's patch: the wake is logged only (a dormant seat stays dormant) and the team is counted here from the match players.
net::VoiceSession::Config voiceConfigFrom(const frontend::SessionVoiceSettings& v);   // the Audio rows -> the voice lane (below)
template <class W, class = void> struct HasQueueLateSeat : std::false_type {};
template <class W> struct HasQueueLateSeat<W, std::void_t<decltype(std::declval<W&>().queueLateSeat(0, std::declval<const game::RemoteSeat&>(), 0u))>> : std::true_type {};
template <class W> void queueLateSeat(W& w, int seat, const game::RemoteSeat& r, uint32_t atTick) {
    if constexpr (HasQueueLateSeat<W>::value) w.queueLateSeat(seat, r, atTick);
    else LOG_WARN("online: World::queueLateSeat missing in this build - seat %d (%s) does not wake at tick %u", seat, r.name.c_str(), atTick);
}
template <class W, class = void> struct HasQueueLeave : std::false_type {};
template <class W> struct HasQueueLeave<W, std::void_t<decltype(std::declval<W&>().queueLeave(0, 0u))>> : std::true_type {};
// A member left mid-match: its slot to a bot (bot fill on: Gameplay's applySlotSwap) or empty (World::queueLeave; Match's Forfeit
// rule then runs by itself), at the agreed tick on every peer.
template <class W> void seatLeft(W& w, int seat, uint32_t untilTick, bool toBot) {
    if (toBot) { game::SlotSwap s; s.matchPlayer = seat; s.toBot = true; s.atTick = (int64_t)untilTick; w.applySlotSwap(s); return; }
    if constexpr (HasQueueLeave<W>::value) w.queueLeave(seat, untilTick);
    else LOG_WARN("online: World::queueLeave missing in this build - seat %d's participant stays (idle) after tick %u", seat, untilTick);
}
template <class W, class = void> struct HasSmallerTeam : std::false_type {};
template <class W> struct HasSmallerTeam<W, std::void_t<decltype(std::declval<const W&>().smallerTeam())>> : std::true_type {};
template <class W> int smallerTeamOf(const W& w) {
    if constexpr (HasSmallerTeam<W>::value) return w.smallerTeam();
    else {
        int n[2] = {0, 0};
        for (const auto& p : w.match().players()) if (p.team == 0 || p.team == 1) ++n[p.team];
        return n[1] < n[0] ? 1 : 0;
    }
}
template <class W, class = void> struct HasSeatPlayer : std::false_type {};
template <class W> struct HasSeatPlayer<W, std::void_t<decltype(std::declval<const W&>().seatPlayer(0))>> : std::true_type {};
template <class W> int seatPlayerOf(const W& w, int seat) {
    if constexpr (HasSeatPlayer<W>::value) return w.seatPlayer(seat);
    else return seat;   // the launch's seat order: dormant seats are humans too
}

// [Systems, online phase 2] The online match from the frontend: the lockstep driver over the session's match view (match player i =
// seat i on every peer), started right after World::launchMatch of an online launch. The launch hash is phase 1's: the host's
// launch URL + seed + seats + this build, so a peer on another build / URL fails at HELLO (JoinFailed{Version} via the session).
void Application::startNetDriver(const std::string& launchUrl, uint32_t seed, int seats, int localSeat) {
    netDriver_.reset();
    if (seats < 2) {   // no remote human: the local loop runs the match (fillOnlineLaunch sends no seat order then)
        LOG_INFO("online: %d seat(s) - no lockstep driver (the local loop runs the match)", seats);
        return;
    }
    if (!udpSession_ || !udpSession_->matchReady() || !udpSession_->matchTransport()) {
        LOG_WARN("online: an online launch without a ready session roster - the match runs without the lockstep driver");
        return;
    }
    if (udpSession_->matchSeats() != seats || udpSession_->matchSelf() != localSeat) {
        LOG_WARN("online: the launch has %d seats / local %d but the session roster %d / %d - no driver", seats, localSeat,
                 udpSession_->matchSeats(), udpSession_->matchSelf());
        return;
    }
    const std::string url = udpSession_->launchUrl().empty() ? launchUrl : udpSession_->launchUrl();
    uint64_t lh = 1469598103934665603ULL;
    lh = fnv(lh, url.data(), url.size()); lh = fnv(lh, &seed, sizeof seed); lh = fnv(lh, &seats, sizeof seats);
    const char* build = __DATE__ " " __TIME__;
    lh = fnv(lh, build, std::strlen(build));
    net::NetMatchDriver::Config c;
    c.seats = seats; c.self = localSeat; c.launchHash = lh; c.delay = 0; c.hashEvery = 60;
    if (const char* rp = std::getenv("WFC_NET_RECORD")) c.recordPath = rp;
    c.voice = true;   // everyone hears; the mode decides whether this player talks (Off = listen only)
    c.voiceMic = udpSession_->voiceSettings().mode != frontend::SessionVoiceMode::Off;
    c.voiceCfg = voiceConfigFrom(udpSession_->voiceSettings());
    c.voiceMuted = [this](int seat) { return udpSession_ && udpSession_->isMuted(udpSession_->matchMember(seat)); };
    c.step = [this](float dt) {
        world_.tick(dt);
        gameMode_.tick(world_, dt);
        if (frontend_) { driveLifecycleTest(dt); routeMatchToFrontend(dt); }
    };
    c.onSelection = [this](int seat, const game::CharacterSelection& sel, uint32_t atTick) { queueSelection(world_, seat, sel, atTick); };
    // Join in progress: Reserved seats (dormant humans) from the launch roster; the host keeps the history while any is free.
    c.activeFrom = udpSession_->matchActiveFrom();
    c.lateJoin = udpSession_->lateJoin();
    c.keepHistory = localSeat == 0 && std::find(c.activeFrom.begin(), c.activeFrom.end(), 0u) != c.activeFrom.end();
    c.pickJoinTeam = [this](int) { return smallerTeamOf(world_); };
    c.seatPlayer = [this](int seat) { return seatPlayerOf(world_, seat); };
    c.leaveToBot = [](int) { return true; };   // D16 (Integration 2026-10-23): a leaver's slot always goes to a bot, public and private
    c.onSeatLeave = [this](int seat, uint32_t until, bool toBot) { seatLeft(world_, seat, until, toBot); };
    c.onSeatJoin = [this](int seat, int team, bool local, uint32_t atTick) {
        game::RemoteSeat r;
        r.team = team; r.selected = false;
        if (udpSession_) {
            r.member = udpSession_->matchMember(seat).fingerprint;
            r.name = udpSession_->matchMemberName(seat);
        }
        if (r.name.empty()) r.name = "Player";
        LOG_INFO("online: seat %d (%s) wakes at tick %u on team %d%s", seat, r.name.c_str(), atTick, team, local ? " - this peer" : "");
        queueLateSeat(world_, seat, r, atTick);
    };
    netDriver_ = std::make_unique<net::NetMatchDriver>(world_, *udpSession_->matchTransport(), audio_, c);
    LOG_INFO("online: the lockstep driver runs the match (seat %d of %d, launch hash %016llx)", localSeat, seats, (unsigned long long)lh);
}

// The Audio rows -> the voice lane: volume, who hears this player (TeamRadio: the team by radio; AllRadio: everyone by radio - the
// host's fan-out treats radio as team only today, so AllRadio maps to radio + proximity until the relay knows it; Proximity; Radio +
// Proximity), open mic.
net::VoiceSession::Config voiceConfigFrom(const frontend::SessionVoiceSettings& v) {
    net::VoiceSession::Config c;
    c.volume = std::max(0, std::min(100, v.volume)) / 100.0f;
    using A = frontend::SessionVoiceAudience;
    c.radio = v.audience != A::Proximity;
    c.proximity = v.audience != A::TeamRadio;
    c.openMic = v.mode == frontend::SessionVoiceMode::OpenMic;
    return c;
}

// [Systems] The lobby voice lane: in a session with no match driver (the game lobby, between matches), everyone hears everyone
// (2D radio at the voice volume), push to talk / open mic as in the match, local mute, the talking indicators for the lobby list.
// Created on entering a session, dropped when the match driver takes over or the session ends.
void Application::tickLobbyVoice() {
    const bool want = udpSession_ && !netDriver_ && udpSession_->inLobby() && net::voiceCodecAvailable();
    if (!want) {
        if (lobbyVoice_) { lobbyVoice_.reset(); LOG_INFO("voice: the lobby lane closed"); }
        return;
    }
    net::UdpSession* s = udpSession_;
    const frontend::SessionVoiceSettings& v = s->voiceSettings();
    net::VoiceSession::Config c = voiceConfigFrom(v);
    c.radio = true; c.proximity = false;   // the lobby: everyone, 2D
    if (!lobbyVoice_) {
        net::VoiceSession::Link link{[s] { return s->sessionSeat(); }, [s](const uint8_t* p, size_t n) { s->sendLobbyVoice(p, n); },
                                     [s](std::function<void(int, const uint8_t*, size_t)> f) { s->setLobbyVoiceSink(std::move(f)); }};
        lobbyVoice_ = std::make_unique<net::VoiceSession>(link, audio_, c);
        static const bool noInteract = [] { const char* e = std::getenv("WFC_NOINTERACT"); return e && *e && e[0] != '0'; }();
        if (v.mode != frontend::SessionVoiceMode::Off && !noInteract) lobbyVoice_->setSource(net::createVoiceCapture());
        lobbyVoice_->setMuted([s](int seat) { return s->isMuted(s->memberOfSeat(seat)); });
        LOG_INFO("voice: the lobby lane opened (seat %d)", s->sessionSeat());
    }
    lobbyVoice_->setConfig(c);
    const bool talking = v.mode == frontend::SessionVoiceMode::OpenMic || (v.mode == frontend::SessionVoiceMode::PushToTalk && s->pushToTalk());
    lobbyVoice_->update(talking, nullptr, [](int) { return true; }, nullptr, nullptr, core::Vec3{0, 0, 0}, core::Vec3{1, 0, 0});
    for (int seat : s->lobbySeats()) s->reportVoice(s->memberOfSeat(seat), lobbyVoice_->headsetState(seat), lobbyVoice_->lastVia(seat));
}

void Application::tickNetDriver(double realDt) {
    net::NetMatchDriver& d = *netDriver_;
    d.setHeld(frontend_ && frontend_->flow().simHeld());
    if (udpSession_) {   // push-to-talk / open mic from the session's voice settings
        const frontend::SessionVoiceSettings& v = udpSession_->voiceSettings();
        d.setTalking(v.mode == frontend::SessionVoiceMode::OpenMic || (v.mode == frontend::SessionVoiceMode::PushToTalk && udpSession_->pushToTalk()));
    }
    if (udpSession_ && d.voice()) {   // the Audio rows stay live (volume / audience / open mic changed in the pause menu)
        d.voice()->setConfig(voiceConfigFrom(udpSession_->voiceSettings()));
    }
    d.frame(realDt);
    if (udpSession_ && d.voice()) {   // the talking indicators: every match seat with a member (this player too)
        for (int s = 0; s < udpSession_->matchSeats(); ++s) {
            const frontend::SessionMemberId& m = udpSession_->matchMember(s);
            udpSession_->reportVoice(m, d.voice()->headsetState(s), d.voice()->lastVia(s));
        }
    }
    if (udpSession_) {   // join in progress: the host's tick (the catch-up limit); a late joiner's replay -> CatchUpProgress
        if (d.localSeat() == 0) {
            udpSession_->noteMatchTick((uint32_t)world_.simTick());
            for (int ms : udpSession_->takeDroppedMatchSeats()) d.dropSeat(ms);   // a member lost mid-match: the leave protocol
        }
        if (d.catchingUp()) udpSession_->reportCatchUp(d.catchUpTick(), d.catchUpTarget());
        else if (udpSession_->lateJoin()) udpSession_->reportCatchUp(d.catchUpTick(), d.catchUpTick());   // the last one: done
    }
    const bool over = !world_.matchActive();
    if (d.finished() || over) {
        const int st = d.status();
        if (st != 0 && udpSession_) udpSession_->matchFailed(st, world_.simTick());
        netDriver_.reset();
        if (udpSession_) udpSession_->matchEnded();
        LOG_INFO("online: the lockstep driver ended (status %d%s)", st, over ? ", the match ended" : "");
    }
}

// WFC_UDPHOST=<port> / WFC_UDPJOIN=<host:port> (+ WFC_UDPSEATS=N, default 2; WFC_UDPBIND=<member port>; WFC_MATCH_URL; WFC_NET_SEED;
// WFC_NET_LOG; WFC_MATCH_SECONDS; WFC_NET_HASH_EVERY): a lockstep match through the real online path - the host's UdpSession waits
// for N - 1 members, publishes the lobby (every human slot), sends the launch record (URL + seed + roster) and starts; every peer
// launches the same match from the record (humans in match-seat order, the runNetMatch selections), reports loaded, waits for
// AllLoaded, then NetMatchDriver steps it (real-time pacing) until WFC_MATCH_SECONDS of match time. Exit codes as runNetMatch:
// 0 ended, 3 desync, 4 timeout / peers missing, 5 host lost, 6 launch mismatch, 7 setup. Headless with WFC_NULLRENDER; netshim can
// sit between (the host binds its port, members WFC_UDPBIND; each side is given the shim's port as the other's address).
// Join in progress: the host with WFC_UDPRESERVE=N adds N Reserved slots (WFC_UDPLATELIMIT_S = the catch-up limit, default 300);
// a member with WFC_UDPLATEJOIN=1 joins the session while the match runs, asks joinRunningMatch on MatchInProgress and launches
// from the late launch record (until Gameplay's dormant seats land, a Reserved seat is a plain human idling on empty commands).
void Application::runUdpSessionMatch() {
    using frontend::SessionEvent;
    using T = SessionEvent::Type;
    auto env = [](const char* k, long d) { const char* v = std::getenv(k); return v ? std::atol(v) : d; };
    const char* hostPort = std::getenv("WFC_UDPHOST");
    const char* joinAddr = std::getenv("WFC_UDPJOIN");
    const char* url = std::getenv("WFC_MATCH_URL");
    const int seats = (int)env("WFC_UDPSEATS", 2);
    if (!url || seats < 2) { LOG_ERROR("UDPMATCH: WFC_MATCH_URL and WFC_UDPSEATS >= 2 are needed"); exitCode_ = 7; return; }
    net::UdpSession::Options o;
    o.internet = false; o.bindIp = std::getenv("WFC_UDPBINDIP") ? std::getenv("WFC_UDPBINDIP") : "127.0.0.1";   // loopback by default: no firewall prompt
    if (hostPort) o.hostPort = std::atoi(hostPort);
    o.memberPort = (int)env("WFC_UDPBIND", 0);
    net::UdpSession s(o);
    frontend::SessionMemberInfo mi;
    mi.displayName = hostPort ? "UdpHost" : "UdpMember";
    mi.hasCharacter = true; mi.characterSlot = "Soldier";
    s.setMemberInfo(mi);
    const auto t0 = std::chrono::steady_clock::now();
    auto now = [&t0] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    std::vector<SessionEvent> ev;
    auto pump = [&](double forS, const std::function<bool()>& until) {
        const double end = now() + forS;
        while (now() < end) {
            for (auto& e : s.poll(now())) { LOG_INFO("UDPMATCH <- %s", frontend::sessionEventName(e.type)); ev.push_back(e); }
            if (until && until()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    auto has = [&ev](T t) { return std::any_of(ev.begin(), ev.end(), [t](const SessionEvent& e) { return e.type == t; }); };
    const uint32_t seed = (uint32_t)env("WFC_NET_SEED", 0x5eed0001);
    std::vector<frontend::SessionMemberId> members;
    if (hostPort) {
        s.host(frontend::SessionHostSettings{});
        if (!pump(5.0, [&] { return has(T::HostOpened); })) { LOG_ERROR("UDPMATCH: the host did not open"); exitCode_ = 7; return; }
        pump(120.0, [&] {
            members.clear();
            for (const SessionEvent& e : ev) if (e.type == T::MemberJoined && std::find(members.begin(), members.end(), e.member) == members.end()) members.push_back(e.member);
            return (int)members.size() >= seats - 1;
        });
        if ((int)members.size() < seats - 1) { LOG_ERROR("UDPMATCH: %zu of %d members joined", members.size(), seats - 1); exitCode_ = 4; return; }
        frontend::SessionLobby lobby;
        lobby.gameModeTag = "TDM";
        frontend::SessionSlot me; me.kind = frontend::SessionSlot::Kind::Human; me.member = s.selfId(); me.team = 0;
        lobby.slots.push_back(me);
        for (size_t i = 0; i < members.size(); ++i) {
            frontend::SessionSlot sl; sl.kind = frontend::SessionSlot::Kind::Human; sl.member = members[i]; sl.team = (int)((i + 1) % 2);
            lobby.slots.push_back(sl);
        }
        for (long i = 0; i < env("WFC_UDPRESERVE", 0); ++i) { frontend::SessionSlot sl; sl.kind = frontend::SessionSlot::Kind::Reserved; lobby.slots.push_back(sl); }
        s.setLateJoinLimit((double)env("WFC_UDPLATELIMIT_S", 300));
        s.publishLobby(lobby);
        pump(0.5, nullptr);
        s.setLaunch(url, seed, lobby);
        s.requestStart();
    } else if (joinAddr) {
        s.join(joinAddr);
        if (std::getenv("WFC_UDPLATEJOIN")) {   // join in progress: the lobby says the match runs -> ask for a Reserved seat
            if (!pump(60.0, [&] { return has(T::MatchInProgress) || has(T::Joined) || has(T::JoinFailed); }) || !has(T::MatchInProgress)) {
                LOG_ERROR("UDPMATCH: no running match to join (joined %d)", has(T::Joined) ? 1 : 0); exitCode_ = 4; return;
            }
            s.joinRunningMatch();
        }
        if (!pump(60.0, [&] { return has(T::CountdownStarted) || has(T::JoinFailed) || has(T::HostLost); }) || !has(T::CountdownStarted)) {
            if (has(T::JoinFailed)) for (const SessionEvent& e : ev) if (e.type == T::JoinFailed) LOG_INFO("UDPMATCH: JoinFailed reason %d", (int)e.joinFailure);
            LOG_ERROR("UDPMATCH: no launch from the host (joined %d)", has(T::Joined) ? 1 : 0); exitCode_ = 4; return;
        }
    }
    if (!s.matchReady()) { LOG_ERROR("UDPMATCH: the session roster is not ready"); exitCode_ = 7; return; }
    if (const char* ld = std::getenv("WFC_LOADDELAY_MS")) {   // TEST ONLY: a blocking map load this long, no polling (a cold shader cache)
        LOG_INFO("UDPMATCH: a %s ms blocking load (no session polling)", ld);
        std::this_thread::sleep_for(std::chrono::milliseconds(std::atol(ld)));
    }
    const std::string launchUrl = s.launchUrl().empty() ? std::string(url) : s.launchUrl();
    game::MatchLaunch L;
    if (!game::MatchLaunch::fromURL(launchUrl, L)) { LOG_ERROR("UDPMATCH: bad launch URL %s", launchUrl.c_str()); exitCode_ = 7; return; }
    L.seed = s.launchSeed() ? s.launchSeed() : seed;
    const int n = s.matchSeats(), self = s.matchSelf();
    L.localTeam = self % 2;
    game::CharacterSelection cs; cs.type = 0; cs.specialty = game::Specialty::Soldier; cs.weapons = {"AssaultRifle", "HomingRocket", "FlakGrenades"};
    const std::vector<uint32_t> activeFrom = s.matchActiveFrom();
    for (int i = 0; i < n; ++i) {   // a Reserved seat: no member at launch on any peer (the joiner too), so every launch is identical
        game::RemoteSeat r; r.name = "Seat" + std::to_string(i); r.team = i % 2;
        const bool reserved = (size_t)i < activeFrom.size() && activeFrom[(size_t)i] == 0;
        r.member = reserved ? std::string() : s.matchMember(i).fingerprint;
        r.selected = !reserved; r.selection = cs;
        r.dormant = reserved;   // Gameplay's dormant seat: no pawn / team / score until World::queueLateSeat wakes it
        L.humans.push_back(r);
    }
    L.localSeat = self; L.online = true;
    world_.launchMatch(L);
    if (world_.localMatchPlayer() != self) { LOG_ERROR("UDPMATCH: local seat %d is match player %d", self, world_.localMatchPlayer()); exitCode_ = 6; return; }
    s.reportLoaded(1, 2);
    if (!pump(60.0, [&] { return has(T::AllLoaded) || has(T::HostLost) || has(T::ConnectionLost); }) || !has(T::AllLoaded)) {
        LOG_ERROR("UDPMATCH: the load gate did not open"); exitCode_ = 4; return;
    }
    uint64_t lh = 1469598103934665603ULL;
    lh = fnv(lh, launchUrl.data(), launchUrl.size()); lh = fnv(lh, &L.seed, sizeof L.seed); lh = fnv(lh, &n, sizeof n);
    const char* build = __DATE__ " " __TIME__;
    lh = fnv(lh, build, std::strlen(build));
    net::NetMatchDriver::Config c;
    c.seats = n; c.self = self; c.launchHash = lh; c.delay = (int)env("WFC_NET_DELAY", 0); c.hashEvery = (int)env("WFC_NET_HASH_EVERY", 60);
    c.peerTimeoutS = (double)env("WFC_NET_TIMEOUT_S", 10);
    if (const char* lp = std::getenv("WFC_NET_LOG")) c.logPath = lp;
    if (const char* rp = std::getenv("WFC_NET_RECORD")) c.recordPath = rp;
    c.voice = false;
    c.step = [this](float dt) { world_.tick(dt); gameMode_.tick(world_, dt); };
    c.onSelection = [this](int seat, const game::CharacterSelection& sel, uint32_t atTick) { queueSelection(world_, seat, sel, atTick); };
    c.activeFrom = activeFrom; c.lateJoin = s.lateJoin();
    c.keepHistory = self == 0 && std::find(activeFrom.begin(), activeFrom.end(), 0u) != activeFrom.end();
    c.pickJoinTeam = [this](int) { return smallerTeamOf(world_); };
    c.seatPlayer = [this](int seat) { return seatPlayerOf(world_, seat); };
    c.leaveToBot = [](int) { return std::getenv("WFC_UDPLEAVE_EMPTY") == nullptr; };   // as the game (D16: a bot); WFC_UDPLEAVE_EMPTY=1: empty + Forfeit
    c.onSeatLeave = [this](int seat, uint32_t until, bool toBot) {
        LOG_INFO("UDPMATCH seat %d leaves at tick %u (%s)", seat, until, toBot ? "to a bot" : "empty");
        seatLeft(world_, seat, until, toBot);
    };
    bool pickAfterWake = false;
    c.onSeatJoin = [this, &s, &pickAfterWake](int seat, int team, bool local, uint32_t atTick) {
        game::RemoteSeat r; r.team = team; r.member = s.matchMember(seat).fingerprint; r.name = s.matchMemberName(seat);
        LOG_INFO("UDPMATCH seat %d (%s) wakes at tick %u on team %d%s", seat, r.name.c_str(), atTick, team, local ? " - this peer" : "");
        queueLateSeat(world_, seat, r, atTick);
        if (local) pickAfterWake = true;   // the woken joiner goes through Choose Character (an event, as in the game)
    };
    net::NetMatchDriver drv(world_, *s.matchTransport(), nullptr, c);
    long catchUpEvents = 0;
    const float matchSeconds = (float)std::atof(std::getenv("WFC_MATCH_SECONDS") ? std::getenv("WFC_MATCH_SECONDS") : "60");
    auto last = std::chrono::steady_clock::now();
    bool started = false, reachedEnd = false, hostLostEarly = false, leftByChoice = false;
    long steps = 0;
    while (!drv.finished()) {
        for (auto& e : s.poll(now())) { LOG_INFO("UDPMATCH <- %s (in match)", frontend::sessionEventName(e.type)); ev.push_back(e); }
        const auto t = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(t - last).count();
        last = t;
        steps += drv.frame(dt);
        if (pickAfterWake && !drv.catchingUp()) { pickAfterWake = false; drv.chooseCharacter(cs); LOG_INFO("UDPMATCH: the joiner picks its character"); }
        if (self == 0) { s.noteMatchTick((uint32_t)world_.simTick()); for (int ms : s.takeDroppedMatchSeats()) drv.dropSeat(ms); }
        // WFC_UDPLEAVE_AT=<match s> (a member): the pause menu's Leave Match - ISession::leave() (the session's LEAVE to the host),
        // then the driver's BYE - at that match time, exit 0. The host and the others play on (the leave protocol).
        static const float leaveAt = std::getenv("WFC_UDPLEAVE_AT") ? (float)std::atof(std::getenv("WFC_UDPLEAVE_AT")) : 0.0f;
        if (leaveAt > 0.0f && self != 0 && world_.matchActive() && world_.match().elapsedTime() >= leaveAt) {
            LOG_INFO("UDPMATCH: seat %d leaves the match at tick %lld (Leave Match)", self, (long long)world_.simTick());
            s.leave();
            leftByChoice = true;
            break;
        }
        if (s.lateJoin()) s.reportCatchUp(drv.catchUpTick(), drv.catchingUp() ? drv.catchUpTarget() : drv.catchUpTick());
        for (const SessionEvent& e : ev) if (e.type == T::CatchUpProgress) ++catchUpEvents;
        ev.erase(std::remove_if(ev.begin(), ev.end(), [](const SessionEvent& e) { return e.type == T::CatchUpProgress; }), ev.end());
        const bool inProgress = world_.matchActive() && world_.match().state() == game::Match::State::InProgress;
        if (inProgress) started = true;
        if ((started && !inProgress) || (inProgress && world_.match().elapsedTime() >= matchSeconds)) { reachedEnd = true; break; }
        if (inProgress && world_.match().elapsedTime() >= matchSeconds - 1.0f) reachedEnd = true;   // the host's normal end may race its leave
        if (has(T::HostLost) || has(T::ConnectionLost)) { hostLostEarly = !reachedEnd; break; }
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    const int st = drv.status();
    LOG_INFO("UDPMATCH done: %ld steps, seat %d of %d, status %d, D %d, rtt %.1f ms%s", steps, self, n, st, drv.delay(), drv.rttMs(),
             s.lateJoin() ? (", late join, " + std::to_string(catchUpEvents) + " CatchUpProgress events").c_str() : "");
    exitCode_ = leftByChoice ? 0 : st != 0 ? st : (hostLostEarly ? (has(T::HostLost) ? 5 : 4) : 0);   // the host leaving at the normal end = 0
}

// WFC_UDPSESSIONTEST (no device, no window, loopback only - 127.0.0.1 binds, so no firewall prompt): two net::UdpSessions in this
// process. Host -> HostOpened; join by address -> JoinProgress, Joined (member) + MemberJoined (host), the member's info ->
// MemberJoined (update); publishLobby -> LobbyState; setLaunch + requestStart -> CountdownStarted (launch record) and the match
// seats; a lockstep-type packet both ways through matchTransport() in match seats; reportLoaded on both -> PeerLoaded / AllLoaded,
// simHeld; the member leaves -> PeerDropped{Left} on the host. Then a bad code -> JoinFailed{Invalid}, a dead address ->
// JoinFailed{Timeout} (3 s). One SUMMARY line.
void Application::runUdpSessionTest() {
    using frontend::SessionEvent;
    using T = SessionEvent::Type;
    int fails = 0;
    auto check = [&fails](bool ok, const char* what) { LOG_INFO("UDPSESSIONTEST %s: %s", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    net::UdpSession::Options o;
    o.bindIp = "127.0.0.1"; o.hostPort = 14100; o.joinTimeoutS = 3.0;
    o.internet = false;   // no router (UPnP) / internet (STUN) contact from a test
    {   // the internet code: public + LAN endpoint, both back out, LAN first; typo / prefix handling
        const std::string code = net::UdpSession::internetCode("203.0.113.7:14000", "192.168.1.23:14000");
        std::vector<std::string> eps;
        check(code.size() == 23 && net::UdpSession::decodeTargets(code, eps) && eps.size() == 2 && eps[0] == "192.168.1.23:14000" &&
                  eps[1] == "203.0.113.7:14000", "internet code round trip (LAN endpoint first)");
        std::vector<std::string> e2;
        std::string lower;
        for (char ch : code) lower += (char)std::tolower((unsigned char)ch);
        check(net::UdpSession::decodeTargets("WFC-" + lower, e2) && e2 == eps, "internet code: lower case + WFC- prefix accepted");
        LOG_INFO("UDPSESSIONTEST internet code %s", code.c_str());
    }
    net::UdpSession hostS(o), memS(o);
    const auto t0 = std::chrono::steady_clock::now();
    auto now = [&t0] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    std::vector<SessionEvent> hostEv, memEv;
    auto pump = [&](double forS, const std::function<bool()>& until) {
        const double end = now() + forS;
        while (now() < end) {
            for (auto& e : hostS.poll(now())) { LOG_INFO("UDPSESSIONTEST host  <- %s", frontend::sessionEventName(e.type)); hostEv.push_back(e); }
            for (auto& e : memS.poll(now())) { LOG_INFO("UDPSESSIONTEST member <- %s", frontend::sessionEventName(e.type)); memEv.push_back(e); }
            if (until && until()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    auto has = [](const std::vector<SessionEvent>& v, T t) { return std::any_of(v.begin(), v.end(), [t](const SessionEvent& e) { return e.type == t; }); };
    auto find = [](const std::vector<SessionEvent>& v, T t) -> const SessionEvent* {
        for (auto it = v.rbegin(); it != v.rend(); ++it) if (it->type == t) return &*it;
        return nullptr;
    };

    frontend::SessionMemberInfo hi; hi.displayName = "HostPC"; hostS.setMemberInfo(hi);
    frontend::SessionMemberInfo mi; mi.displayName = "MemberPC"; memS.setMemberInfo(mi);
    hostS.host(frontend::SessionHostSettings{});
    pump(1.0, [&] { return has(hostEv, T::HostOpened); });
    const SessionEvent* ho = find(hostEv, T::HostOpened);
    check(ho && !ho->joinCode.empty(), "HostOpened with a LAN code");
    check(ho && ho->member == hostS.selfId() && !ho->member.fingerprint.empty(), "HostOpened carries the host's own member id");
    if (ho) {
        std::string back;
        check(net::UdpSession::decodeTarget(ho->joinCode, back) && back == ho->address, "the LAN code decodes to the host address");
        LOG_INFO("UDPSESSIONTEST host address %s code %s", ho->address.c_str(), ho->joinCode.c_str());
    }
    memS.join("127.0.0.1:14100");
    pump(5.0, [&] { return has(memEv, T::Joined) && has(hostEv, T::MemberJoined); });
    check(has(memEv, T::JoinProgress), "member: JoinProgress");
    check(has(memEv, T::Joined), "member: Joined");
    const SessionEvent* mj = find(hostEv, T::MemberJoined);
    check(mj && mj->member == memS.selfId() && mj->info.displayName == "MemberPC", "host: MemberJoined with the member's id and name");
    const SessionEvent* jd = find(memEv, T::Joined);
    check(jd && jd->member == memS.selfId(), "Joined carries the member's own id");
    mi.hasCharacter = true; mi.characterSlot = "Soldier"; mi.mapVote = 2; memS.setMemberInfo(mi);
    hostEv.clear();
    pump(3.0, [&] { const SessionEvent* e = find(hostEv, T::MemberJoined); return e && e->info.hasCharacter; });
    { const SessionEvent* e = find(hostEv, T::MemberJoined); check(e && e->info.characterSlot == "Soldier" && e->info.mapVote == 2, "host: the member's updated info incl. its map vote (MemberJoined again)"); }

    frontend::SessionLobby lobby;
    lobby.gameModeTag = "TDM"; lobby.mapId = 3; lobby.settings["PointsToWin"] = "1000";
    lobby.voteMaps = {3, 5, 8}; lobby.voteCounts = {0, 1, 2, 0}; lobby.voteRound = 1; lobby.voteOpen = true; lobby.voteWinner = -1;
    frontend::SessionSlot s0; s0.kind = frontend::SessionSlot::Kind::Human; s0.member = hostS.selfId(); s0.team = 0;
    frontend::SessionSlot s1; s1.kind = frontend::SessionSlot::Kind::Human; s1.member = memS.selfId(); s1.team = 1;
    frontend::SessionSlot b; b.kind = frontend::SessionSlot::Kind::Bot; b.team = 0;
    lobby.slots = {s0, b, s1};
    hostS.publishLobby(lobby);
    pump(3.0, [&] { const SessionEvent* e = find(memEv, T::LobbyState); return e && e->lobby.slots.size() == 3; });   // the published one
    { const SessionEvent* e = find(memEv, T::LobbyState);
      check(e && e->lobby.slots.size() == 3 && e->lobby.settings.count("PointsToWin") && e->lobby.mapId == 3, "member: LobbyState with the slots and settings");
      check(e && e->lobby.voteMaps == std::vector<int>({3, 5, 8}) && e->lobby.voteCounts == std::vector<int>({0, 1, 2, 0}) && e->lobby.voteRound == 1 &&
            e->lobby.voteOpen && e->lobby.voteWinner == -1, "member: LobbyState carries the map vote (options, counts, round, open)"); }

    hostS.setLaunch("MP_IAC_Streets?GameModeTag=TDM?PointsToWin=1000", 77u, lobby);
    hostS.requestStart();
    pump(3.0, [&] { return has(memEv, T::CountdownStarted); });
    { const SessionEvent* e = find(memEv, T::CountdownStarted);
      check(e && e->seed == 77u && e->launchUrl.find("MP_IAC_Streets") == 0 && e->lobby.slots.size() == 3, "member: CountdownStarted with the launch record"); }
    check(hostS.matchSeats() == 2 && hostS.matchSelf() == 0 && memS.matchSeats() == 2 && memS.matchSelf() == 1, "match seats: host 0, member 1 (bots excluded)");
    check(memS.matchMember(0) == hostS.selfId() && hostS.matchMember(1) == memS.selfId(), "match seat -> member id on both");
    bool gotM = false, gotH = false;
    if (net::INetTransport* hv = hostS.matchTransport()) { const uint8_t pkt[3] = {1, 9, 9}; hv->sendUnreliable(1, pkt, 3); }
    if (net::INetTransport* mv = memS.matchTransport()) { const uint8_t pkt[3] = {3, 7, 7}; mv->sendReliable(0, pkt, 3); }
    pump(3.0, [&] {
        net::NetPacket p;
        if (net::INetTransport* mv = memS.matchTransport()) while (mv->poll(p)) gotM |= p.from == 0 && p.data.size() == 3 && p.data[0] == 1;
        if (net::INetTransport* hv = hostS.matchTransport()) while (hv->poll(p)) gotH |= p.from == 1 && p.data.size() == 3 && p.data[0] == 3;
        return gotM && gotH;
    });
    check(gotM && gotH, "lockstep packets cross the match view both ways, in match seats");

    memS.reportLoaded(1, 2);
    check(memS.simHeld(), "member: simHeld after reportLoaded");
    pump(3.0, [&] { return has(hostEv, T::PeerLoaded); });
    hostS.reportLoaded(1, 2);
    pump(3.0, [&] { return has(memEv, T::AllLoaded) && has(hostEv, T::AllLoaded); });
    check(has(hostEv, T::AllLoaded) && has(memEv, T::AllLoaded), "AllLoaded on both");
    check(!hostS.simHeld() && !memS.simHeld(), "simHeld released at AllLoaded");
    check(has(memEv, T::PeerLoaded), "member: PeerLoaded (the host)");

    memS.leave();
    pump(3.0, [&] { return has(hostEv, T::PeerDropped); });
    { const SessionEvent* e = find(hostEv, T::PeerDropped);
      check(e && e->member == memS.selfId() && e->leaveReason == frontend::SessionLeaveReason::Left, "host: PeerDropped{Left} in the match"); }
    hostS.leave();

    {   // LAN discovery: a public host answers findGames (discovery aimed at 127.0.0.1 here, the broadcast address in the game)
        net::UdpSession::Options lo = o; lo.discoverAddr = "127.0.0.1"; lo.hostPort = 14100;
        net::UdpSession pub(lo), browser(lo);
        std::vector<SessionEvent> pe, be;
        frontend::SessionMemberInfo pi; pi.displayName = "PublicHost"; pub.setMemberInfo(pi);
        frontend::SessionHostSettings hs; hs.isPublic = true; hs.playlist = "TeamDeathmatch"; hs.gameModeTag = "TDM";
        pub.host(hs);
        const double end0 = now() + 1.0;
        while (now() < end0) { for (auto& e : pub.poll(now())) pe.push_back(e); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        frontend::SessionGameQuery q; q.playlist = "TeamDeathmatch";
        browser.findGames(q);
        const double end1 = now() + 4.0;
        while (now() < end1 && !has(be, T::GameList)) {
            for (auto& e : pub.poll(now())) pe.push_back(e);
            for (auto& e : browser.poll(now())) be.push_back(e);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const SessionEvent* gl = find(be, T::GameList);
        check(gl && gl->games.size() == 1 && gl->games[0].lan && gl->games[0].hostName == "PublicHost" && gl->games[0].pingMs >= 0 && !gl->serviceReachable,
              "LAN discovery: findGames lists the public LAN host with a ping (no service)");
        pub.leave(); browser.leave();
    }
    {   // Quick Match with nothing to join hosts a public lobby: the host must own seat 0 on its own host port, and a launch roster
        // of its slot + Reserved seats gives exactly that many match seats with the host at 0 (the pt9 two-window failure, 2026-10-23)
        net::UdpSession::Options qo = o; qo.discoverAddr = "127.0.0.1"; qo.hostPort = 24160;
        net::UdpSession qh(qo);
        std::vector<SessionEvent> qe;
        frontend::SessionMemberInfo qi; qi.displayName = "QuickHost"; qh.setMemberInfo(qi);
        qh.quickMatch("ReproTDM");
        const double endq = now() + 6.0;
        while (now() < endq && !has(qe, T::HostOpened)) { for (auto& e : qh.poll(now())) qe.push_back(e); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        const SessionEvent* qho = find(qe, T::HostOpened);
        check(qho && qho->address.find(":2416") != std::string::npos, "quick match with nothing to join: hosts on its own host port (not the search port)");
        frontend::SessionLobby ql;
        frontend::SessionSlot qs; qs.kind = frontend::SessionSlot::Kind::Human; qs.member = qh.selfId(); qs.team = 0;
        ql.slots.push_back(qs);
        for (int i = 0; i < 3; ++i) { frontend::SessionSlot r; r.kind = frontend::SessionSlot::Kind::Reserved; ql.slots.push_back(r); }
        qh.publishLobby(ql);
        qh.setLaunch("MP_IAC_Streets?GameModeTag=TDM", 9u, ql);
        qh.requestStart();
        check(qh.matchSeats() == 4 && qh.matchSelf() == 0, "quick-match host: 1 human + 3 Reserved = 4 match seats, the host at seat 0");
        LOG_INFO("UDPSESSIONTEST quick-match host: %s, %d match seats, self %d", qho ? qho->address.c_str() : "-", qh.matchSeats(), qh.matchSelf());
        qh.leave();
    }
    if (const char* svc = std::getenv("WFC_UDPSESSIONTEST_SERVICE")) {   // the lobby service (tools/net/lobby_service.py) on loopback
        net::UdpSession::Options so = o; so.discoverAddr = "127.0.0.1"; so.hostPort = 14120;
        net::UdpSession h(so), j(so), q2(so);
        for (net::UdpSession* s : {&h, &j, &q2}) s->setLobbyService(svc);
        std::vector<SessionEvent> he, je, qe;
        auto run = [&](double forS, const std::function<bool()>& until) {
            const double end = now() + forS;
            while (now() < end) {
                for (auto& e : h.poll(now())) he.push_back(e);
                for (auto& e : j.poll(now())) je.push_back(e);
                for (auto& e : q2.poll(now())) qe.push_back(e);
                if (until && until()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        run(0.5, nullptr);   // the service address resolves
        frontend::SessionHostSettings hs; hs.isPublic = true; hs.playlist = "TeamDeathmatch"; hs.gameModeTag = "TDM";
        frontend::SessionMemberInfo hi2; hi2.displayName = "ServiceHost"; h.setMemberInfo(hi2);
        h.host(hs);
        run(4.0, [&] { return has(he, T::HostOpened); });
        const SessionEvent* ho2 = find(he, T::HostOpened);
        std::string letters;
        if (ho2) for (char ch : ho2->joinCode) if (std::isalpha((unsigned char)ch)) letters += ch;
        check(ho2 && ho2->joinCode.size() == 6 && letters.size() == 6, "service: HostOpened carries the service's 6-letter code");
        if (ho2) LOG_INFO("UDPSESSIONTEST service code %s", ho2->joinCode.c_str());
        if (ho2) j.join(ho2->joinCode);
        run(5.0, [&] { return has(je, T::Joined); });
        check(has(je, T::Joined), "service: join by the 6-letter code -> Joined");
        q2.quickMatch("TeamDeathmatch");
        run(8.0, [&] { return has(qe, T::Joined) || has(qe, T::HostOpened); });
        check(has(qe, T::SearchProgress), "quick match: SearchProgress");
        check(has(qe, T::Joined) && !has(qe, T::HostOpened), "quick match: joined the public lobby by ping (did not host)");
        for (net::UdpSession* s : {&q2, &j, &h}) s->leave();
    }

    memEv.clear();
    memS.join("ZZZZZ-ZZZZZ");   // the 2 spare high bits set: rejected
    pump(0.5, [&] { return has(memEv, T::JoinFailed); });
    { const SessionEvent* e = find(memEv, T::JoinFailed); check(e && e->joinFailure == frontend::SessionJoinFailure::Invalid, "a bad code -> JoinFailed{Invalid}"); }
    memEv.clear();
    memS.join("127.0.0.1:14199");   // nobody there
    pump(5.0, [&] { return has(memEv, T::JoinFailed); });
    { const SessionEvent* e = find(memEv, T::JoinFailed); check(e && e->joinFailure == frontend::SessionJoinFailure::Timeout, "a dead address -> JoinFailed{Timeout}"); }
    memS.leave();
    LOG_INFO("UDPSESSIONTEST SUMMARY: %s (%d failed)", fails ? "FAIL" : "PASS", fails);
    exitCode_ = fails ? 1 : 0;
}


// WFC_VOICETEST [Systems] (headless, no device, no network): the voice lane end to end. Two Locksteps over an in-process loopback pair
// (a lossy wrapper drops a share of the unreliable VOICE packets); seat 0 talks a synthetic 440 Hz tone (VoiceSession's test talker,
// 0.3 full scale) for 3 s at real time, seat 1's VoiceSession decodes it (jitter buffer, Opus, concealment) and a tap collects the PCM.
// Quality: per decoded 20 ms frame, a least-squares fit of a 440 Hz sinusoid (its own phase: delay / concealment jumps do not count);
// SNR = fit / residual energy, the median over the decoded (not concealed) frames after the codec's start-up.
// Arms: no loss (median >= 25 dB), 5 % loss (concealment used, median >= 25 dB on the decoded frames), mute (nothing played, the
// indicator still on); the talking indicator
// (headset state 2 while speaking). One SUMMARY line; exit 0 = PASS.
namespace {
class LossyTransport final : public net::INetTransport {
public:
    LossyTransport(net::INetTransport& t, double loss, uint32_t seed) : t_(t), loss_(loss), rng_(seed) {}
    int self() const override { return t_.self(); }
    void sendUnreliable(int peer, const uint8_t* p, size_t n) override {
        rng_ = rng_ * 1664525u + 1013904223u;
        if (n > 0 && p[0] == 7 && (double)(rng_ >> 8) / 16777216.0 < loss_) { ++dropped; return; }   // VOICE packets only
        t_.sendUnreliable(peer, p, n);
    }
    void sendReliable(int peer, const uint8_t* p, size_t n) override { t_.sendReliable(peer, p, n); }
    bool poll(net::NetPacket& out) override { return t_.poll(out); }
    void service() override { t_.service(); }
    net::NetStats stats() const override { return t_.stats(); }
    long dropped = 0;
private:
    net::INetTransport& t_;
    double loss_;
    uint32_t rng_;
};
}  // namespace

void Application::runVoiceTest() {
    int fails = 0;
    auto check = [&fails](bool ok, const char* what) { LOG_INFO("VOICETEST %s: %s", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    if (!net::voiceCodecAvailable()) { check(false, "Opus in this build (WFC_OPUS)"); LOG_INFO("VOICETEST SUMMARY: FAIL"); exitCode_ = 1; return; }
    struct Result { double snrDb = -99.0, amp = 0.0; long frames = 0, concealed = 0, dropped = 0, muted = 0; int state = -1; std::vector<double> snrs, amps; };
    auto arm = [&](double loss, bool mute) {
        Result r;
        auto group = net::createLoopbackGroup(2);
        LossyTransport ta(*group[0], loss, 77u), tb(*group[1], loss, 99u);
        net::Lockstep::Config ca; ca.seats = 2; ca.self = 0; ca.delay = 2;
        net::Lockstep::Config cb = ca; cb.self = 1;
        net::Lockstep la(ta, ca), lb(tb, cb);
        net::VoiceSession va(la, nullptr, net::VoiceSession::Config{}), vb(lb, nullptr, net::VoiceSession::Config{});
        va.setSyntheticTone(440.0f);
        vb.setPlayTap([&](int seat, const net::VoiceFrame& pcm, bool concealed) {
            if (seat != 0) return;
            ++r.frames; if (concealed) { ++r.concealed; return; }
            if (r.frames <= 10) return;   // the codec's start-up
            double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
            for (int i = 0; i < net::kVoiceFrameSamples; ++i) {
                const double ph = 6.283185307 * 440.0 * (double)i / (double)net::kVoiceRate, sn = std::sin(ph), cs = std::cos(ph), y = pcm[(size_t)i];
                ss += sn * sn; sc += sn * cs; cc += cs * cs; ys += y * sn; yc += y * cs;
            }
            const double det = ss * cc - sc * sc;
            if (std::fabs(det) < 1e-9) return;
            const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
            double es = 0, er = 0;
            for (int i = 0; i < net::kVoiceFrameSamples; ++i) {
                const double ph = 6.283185307 * 440.0 * (double)i / (double)net::kVoiceRate, f = a * std::sin(ph) + b * std::cos(ph);
                es += f * f; er += (pcm[(size_t)i] - f) * (pcm[(size_t)i] - f);
            }
            r.snrs.push_back(10.0 * std::log10(es / std::max(er, 1e-9)));
            r.amps.push_back(std::sqrt(a * a + b * b) / 32767.0);
        });
        if (mute) vb.setMuted([](int seat) { return seat == 0; });
        auto team = [](int) { return true; };   // radio: heard 2D (the tap sees it before routing either way)
        const auto t0 = std::chrono::steady_clock::now();
        auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
        while (secs() < 3.4) {
            la.pump(); lb.pump();
            const bool talking = secs() < 3.0;
            va.update(talking, nullptr, team, nullptr, nullptr, core::Vec3{0, 0, 0}, core::Vec3{1, 0, 0});
            vb.update(false, nullptr, team, nullptr, nullptr, core::Vec3{0, 0, 0}, core::Vec3{1, 0, 0});
            if (r.state < 0 && secs() > 1.0) r.state = vb.headsetState(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        r.dropped = ta.dropped; r.muted = vb.stats().muted;
        auto median = [](std::vector<double> v) { if (v.empty()) return -99.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        r.snrDb = median(r.snrs); r.amp = median(r.amps);
        return r;
    };
    const Result clean = arm(0.0, false), lossy = arm(0.05, false), muted = arm(0.0, true);
    LOG_INFO("VOICETEST clean: %ld frames (%ld concealed), SNR %.1f dB, amplitude %.3f (sent 0.300), indicator %d", clean.frames, clean.concealed, clean.snrDb, clean.amp, clean.state);
    LOG_INFO("VOICETEST 5%% loss: %ld frames (%ld concealed, %ld packets dropped), SNR %.1f dB, amplitude %.3f", lossy.frames, lossy.concealed, lossy.dropped, lossy.snrDb, lossy.amp);
    LOG_INFO("VOICETEST muted: %ld frames played, %ld muted packets, indicator %d", muted.frames, muted.muted, muted.state);
    check(clean.frames >= 140 && clean.concealed <= 3 && clean.snrDb >= 25.0 && clean.amp > 0.25 && clean.amp < 0.35,
          "no loss: ~150 frames heard (only the 3-frame tail concealed), median SNR >= 25 dB, the level kept");
    check(lossy.dropped > 0 && lossy.concealed > 3 && lossy.snrDb >= 25.0, "5 % loss: the losses concealed, the decoded frames' median SNR >= 25 dB");
    check(clean.state == 2, "the talking indicator: speaking (2) while the tone plays");
    check(muted.frames == 0 && muted.muted > 0 && muted.state == 2, "muted talker: nothing played, its indicator still shows");

    {   // The lobby lane: real UdpSessions on loopback (a host + 2 members); member 1 talks; the host hears it directly, member 2 through
        // the host's relay (everyone in the lobby hears everyone).
        using T = frontend::SessionEvent::Type;
        net::UdpSession::Options o; o.bindIp = "127.0.0.1"; o.hostPort = 24170; o.internet = false;
        net::UdpSession h(o), m1(o), m2(o);
        const auto t0 = std::chrono::steady_clock::now();
        auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
        std::vector<frontend::SessionEvent> he, e1, e2;
        auto pumpAll = [&] { for (auto& e : h.poll(secs())) he.push_back(e); for (auto& e : m1.poll(secs())) e1.push_back(e); for (auto& e : m2.poll(secs())) e2.push_back(e); };
        auto has = [](const std::vector<frontend::SessionEvent>& v, T t) { for (const auto& e : v) if (e.type == t) return true; return false; };
        h.host(frontend::SessionHostSettings{});
        while (secs() < 3.0 && !has(he, T::HostOpened)) { pumpAll(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        m1.join("127.0.0.1:24170"); m2.join("127.0.0.1:24170");
        while (secs() < 8.0 && !(has(e1, T::Joined) && has(e2, T::Joined))) { pumpAll(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        auto linkOf = [](net::UdpSession* s) {
            return net::VoiceSession::Link{[s] { return s->sessionSeat(); }, [s](const uint8_t* p, size_t n) { s->sendLobbyVoice(p, n); },
                                           [s](std::function<void(int, const uint8_t*, size_t)> f) { s->setLobbyVoiceSink(std::move(f)); }};
        };
        net::VoiceSession::Config vc; vc.radio = true; vc.proximity = false;
        net::VoiceSession vh(linkOf(&h), nullptr, vc), v1(linkOf(&m1), nullptr, vc), v2(linkOf(&m2), nullptr, vc);
        v1.setSyntheticTone(440.0f);
        long hFrames = 0, m2Frames = 0;
        const int talker = m1.sessionSeat();
        vh.setPlayTap([&](int seat, const net::VoiceFrame&, bool c) { if (seat == talker && !c) ++hFrames; });
        v2.setPlayTap([&](int seat, const net::VoiceFrame&, bool c) { if (seat == talker && !c) ++m2Frames; });
        auto team = [](int) { return true; };
        const double start = secs();
        while (secs() - start < 2.4) {
            pumpAll();
            v1.update(secs() - start < 2.0, nullptr, team, nullptr, nullptr, core::Vec3{0, 0, 0}, core::Vec3{1, 0, 0});
            vh.update(false, nullptr, team, nullptr, nullptr, core::Vec3{0, 0, 0}, core::Vec3{1, 0, 0});
            v2.update(false, nullptr, team, nullptr, nullptr, core::Vec3{0, 0, 0}, core::Vec3{1, 0, 0});
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        LOG_INFO("VOICETEST lobby: member seat %d talked 2 s - the host decoded %ld frames, the other member %ld (relayed)", talker, hFrames, m2Frames);
        check(has(e1, T::Joined) && has(e2, T::Joined) && hFrames >= 80 && m2Frames >= 80, "the lobby lane: the host hears a member, the other member through the host's relay");
        for (net::UdpSession* x : {&m1, &m2, &h}) x->leave();
    }
    LOG_INFO("VOICETEST SUMMARY: %s (%d failed)", fails ? "FAIL" : "PASS", fails);
    exitCode_ = fails ? 1 : 0;
}
}  // namespace core
