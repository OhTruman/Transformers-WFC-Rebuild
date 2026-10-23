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

bool Application::netMatchRequested() { return std::getenv("WFC_NET_SLOT") != nullptr; }

void Application::runNetMatch() {
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
    cfg.seats = seats; cfg.self = self; cfg.delay = (int)env("WFC_NET_DELAY", 0); cfg.hashEvery = (int)env("WFC_NET_HASH_EVERY", 60);   // delay 0 = auto from the RTT
    cfg.peerTimeoutS = (double)env("WFC_NET_TIMEOUT_S", 10); cfg.launchHash = lh;
    if (const char* lp = std::getenv("WFC_NET_LOG")) cfg.logPath = lp;
    net::Lockstep ls(*t, cfg);
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
    const auto tRun = std::chrono::steady_clock::now();
    while (ls.status() == net::Lockstep::Status::Running) {
        ls.pump();
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
        if (pace && secsSince(tRun) < (double)n * step) {   // not yet this tick's wall time (real-time pacing)
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

// [Systems, online phase 2] The online match from the frontend: the lockstep driver over the session's match view (match player i =
// seat i on every peer), started right after World::launchMatch of an online launch. The launch hash is phase 1's: the host's
// launch URL + seed + seats + this build, so a peer on another build / URL fails at HELLO (JoinFailed{Version} via the session).
void Application::startNetDriver(const std::string& launchUrl, uint32_t seed, int seats, int localSeat) {
    netDriver_.reset();
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
    c.voice = udpSession_->voiceSettings().mode != frontend::SessionVoiceMode::Off;
    c.step = [this](float dt) {
        world_.tick(dt);
        gameMode_.tick(world_, dt);
        if (frontend_) { driveLifecycleTest(dt); routeMatchToFrontend(dt); }
    };
    c.onSelection = [this](int seat, const game::CharacterSelection& sel, uint32_t atTick) { queueSelection(world_, seat, sel, atTick); };
    netDriver_ = std::make_unique<net::NetMatchDriver>(world_, *udpSession_->matchTransport(), audio_, c);
    LOG_INFO("online: the lockstep driver runs the match (seat %d of %d, launch hash %016llx)", localSeat, seats, (unsigned long long)lh);
}

void Application::tickNetDriver(double realDt) {
    net::NetMatchDriver& d = *netDriver_;
    d.setHeld(frontend_ && frontend_->flow().simHeld());
    if (udpSession_) {   // push-to-talk / open mic from the session's voice settings
        const frontend::SessionVoiceSettings& v = udpSession_->voiceSettings();
        d.setTalking(v.mode == frontend::SessionVoiceMode::OpenMic || (v.mode == frontend::SessionVoiceMode::PushToTalk && udpSession_->pushToTalk()));
    }
    d.frame(realDt);
    const bool over = !world_.matchActive();
    if (d.finished() || over) {
        const int st = d.status();
        if (st != 0 && udpSession_) udpSession_->matchFailed(st, world_.simTick());
        netDriver_.reset();
        if (udpSession_) udpSession_->matchEnded();
        LOG_INFO("online: the lockstep driver ended (status %d%s)", st, over ? ", the match ended" : "");
    }
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
    mi.hasCharacter = true; mi.characterSlot = "Soldier"; memS.setMemberInfo(mi);
    hostEv.clear();
    pump(3.0, [&] { const SessionEvent* e = find(hostEv, T::MemberJoined); return e && e->info.hasCharacter; });
    { const SessionEvent* e = find(hostEv, T::MemberJoined); check(e && e->info.characterSlot == "Soldier", "host: the member's updated info (MemberJoined again)"); }

    frontend::SessionLobby lobby;
    lobby.gameModeTag = "TDM"; lobby.mapId = 3; lobby.settings["PointsToWin"] = "1000";
    frontend::SessionSlot s0; s0.kind = frontend::SessionSlot::Kind::Human; s0.member = hostS.selfId(); s0.team = 0;
    frontend::SessionSlot s1; s1.kind = frontend::SessionSlot::Kind::Human; s1.member = memS.selfId(); s1.team = 1;
    frontend::SessionSlot b; b.kind = frontend::SessionSlot::Kind::Bot; b.team = 0;
    lobby.slots = {s0, b, s1};
    hostS.publishLobby(lobby);
    pump(3.0, [&] { return has(memEv, T::LobbyState); });
    { const SessionEvent* e = find(memEv, T::LobbyState);
      check(e && e->lobby.slots.size() == 3 && e->lobby.settings.count("PointsToWin") && e->lobby.mapId == 3, "member: LobbyState with the slots and settings"); }

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

}  // namespace core
