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
// Exit codes: 0 the match ended / the run's end point, 3 desync, 4 peer timeout, 5 host lost, 6 launch mismatch, 7 transport error.
#include "core/Application.h"
#include "core/Log.h"
#include "game/World.h"
#include "net/Lockstep.h"
#include "net/NetTransport.h"
#include "net/VoiceCodec.h"
#include "net/VoiceFrames.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
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
    if (!bind || !url || seats < 2 || self < 0 || self >= seats) {
        LOG_ERROR("NET: WFC_NET_SLOT needs WFC_NET_BIND, WFC_MATCH_URL and 0 <= slot < WFC_NET_SEATS (>= 2)");
        exitCode_ = 7; return;
    }
    std::string err;
    std::unique_ptr<net::INetTransport> t = net::createUdpTransport(self, bind, parsePeers(std::getenv("WFC_NET_PEERS"), self, seats), err);
    if (!t) { LOG_ERROR("NET transport: %s", err.c_str()); exitCode_ = 7; return; }

    // The launch record (ONLINE_MP_ARCHITECTURE 10.3): the URL, the seed and the seat table, identical on every peer; its hash is the
    // HELLO check. [PENDING Gameplay: MatchLaunch::humans + localSeat build every peer's match players in the host's seat order; until
    // then the seats are this machine's local player plus Remote seats "SEAT<n>", whose match-player order differs per peer.]
    game::MatchLaunch L;
    if (!game::MatchLaunch::fromURL(url, L)) { LOG_ERROR("NET: bad WFC_MATCH_URL"); exitCode_ = 7; return; }
    L.seed = (uint32_t)env("WFC_NET_SEED", 0x5eed0001);
    if (L.seed == 0) L.seed = 1;   // 0 = offline
    L.localTeam = self % 2;
    game::CharacterSelection cs; cs.type = 0; cs.specialty = game::Specialty::Soldier; cs.weapons = {"AssaultRifle", "HomingRocket", "FlakGrenades"};
    for (int s = 0; s < seats; ++s) {
        if (s == self) continue;
        game::RemoteSeat r; r.name = "Seat" + std::to_string(s); r.team = s % 2; r.member = "SEAT" + std::to_string(s);
        r.selected = true; r.selection = cs;
        L.remotes.push_back(r);
    }
    uint64_t lh = 1469598103934665603ULL;
    lh = fnv(lh, url, std::strlen(url)); lh = fnv(lh, &L.seed, sizeof L.seed); lh = fnv(lh, &seats, sizeof seats);
    const char* build = __DATE__ " " __TIME__;   // the same exe on every peer (a different build = a different sim)
    lh = fnv(lh, build, std::strlen(build));
    world_.launchMatch(L);
    world_.match().selectCharacter(world_.localMatchPlayer(), cs);
    std::vector<int> seatMp((size_t)seats, -1);   // wire seat -> match player
    seatMp[(size_t)self] = world_.localMatchPlayer();
    const auto& ps = world_.match().players();
    for (size_t i = 0; i < ps.size(); ++i)
        for (int s = 0; s < seats; ++s)
            if (ps[i].sessionMember == "SEAT" + std::to_string(s)) seatMp[(size_t)s] = (int)i;
    std::vector<int> order(seats);   // apply order: ascending match player
    for (int s = 0; s < seats; ++s) order[(size_t)s] = s;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return seatMp[(size_t)a] < seatMp[(size_t)b]; });
    for (int s = 0; s < seats; ++s) LOG_INFO("NET seat %d -> match player %d%s", s, seatMp[(size_t)s], s == self ? " (local)" : "");

    net::Lockstep::Config cfg;
    cfg.seats = seats; cfg.self = self; cfg.delay = (int)env("WFC_NET_DELAY", 0); cfg.hashEvery = (int)env("WFC_NET_HASH_EVERY", 60);   // delay 0 = auto from the RTT
    cfg.peerTimeoutS = (double)env("WFC_NET_TIMEOUT_S", 10); cfg.launchHash = lh;
    if (const char* lp = std::getenv("WFC_NET_LOG")) cfg.logPath = lp;
    net::Lockstep ls(*t, cfg);
    const long desyncAt = env("WFC_NET_DESYNC_AT", -1);

    // Wait for every peer's HELLO (the load gate: no tick before everyone is here).
    const auto t0 = std::chrono::steady_clock::now();
    auto secsSince = [](std::chrono::steady_clock::time_point a) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count(); };
    while (!ls.everyoneHere() && ls.status() == net::Lockstep::Status::Running) {
        ls.pump();
        if (secsSince(t0) > cfg.peerTimeoutS * 3.0) { LOG_ERROR("NET: peers did not arrive in %.0f s", cfg.peerTimeoutS * 3.0); exitCode_ = 4; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    LOG_INFO("NET all %d seats here after %.2f s", seats, secsSince(t0));

    const float step = clock_.stepSeconds();
    const float matchSeconds = (float)std::atof(std::getenv("WFC_MATCH_SECONDS") ? std::getenv("WFC_MATCH_SECONDS") : "0");
    uint32_t nextLocal = 1;
    const bool pace = std::getenv("WFC_NET_PACE") != nullptr;
    long steps = 0;
    bool started = false;
    const auto tRun = std::chrono::steady_clock::now();
    while (ls.status() == net::Lockstep::Status::Running) {
        ls.pump();
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
    const int st = (int)ls.status();
    if (st == 0) ls.bye();
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

}  // namespace core
