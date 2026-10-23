// WFC_KISMETTEST (headless, no map): the campaign Kismet VM (game/Kismet.h). (1) Synthetic graphs for the stock op semantics: Delay
// timing / Stop / Pause, Gate AutoCloseCount, RemoteEvent fan-out, an event's MaxTriggerCount, SeqCond_Increment, a sub-sequence
// input. (2) D5_OME's exported graph (AssetTools next_map/D5_OME_kismet.json) started from its first checkpoint with a stub host:
// it loads, the start runs (Checkpoint_4578 set by PreGameplaySetup), 120 s of ticks run with no impulse-loop cap hit. One SUMMARY.
#include "core/Application.h"
#include "core/Log.h"
#include "game/Kismet.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

// The AssetTools manifests (WFC_MANIFESTS, else the dev tree): as ChassisDef.cpp's manifestRoot().
static std::string kismetManifestRoot() {
    if (const char* e = std::getenv("WFC_MANIFESTS")) return e;
    return "F:/Transformers Rebuild/AssetTools/manifests";
}

namespace core {

void Application::runKismetTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) ++fails; LOG_INFO("KISMETTEST %s %s", ok ? "PASS" : "FAIL", what); };
    auto parse = [](const std::string& s) { assets::Json j; assets::Json::parse(s, j); return j; };
    // A synthetic level: ops as the exporter writes them (object / class / authored / inputs / outputs / variables / incoming).
    auto graph = [&](const std::string& objects) { return parse("{\"levels\":{\"L\":[" + objects + "]}}"); };
    struct Recorder : game::KismetVM::Host {
        std::vector<std::pair<float, std::string>> hits;
        bool activate(game::KismetVM& vm, int op, int) override { hits.push_back({vm.time(), vm.op(op).path}); return true; }
    };
    const float dt = 1.0f / 60.0f;
    {   // Delay: Start -> Finished after Duration; Stop -> Aborted; Pause holds the timer
        game::KismetVM vm; Recorder r; vm.setHost(&r);
        vm.load(graph(
            "{\"object\":\"S.E\",\"class\":\"SeqEvent_RemoteEvent\",\"authored\":{\"EventName\":\"go\"},\"inputs\":[],\"outputs\":{\"Out\":[{\"op\":\"S.D\",\"input_index\":0}]},\"variables\":{},\"incoming\":[]},"
            "{\"object\":\"S.D\",\"class\":\"SeqAct_Delay\",\"authored\":{\"Duration\":0.5},\"inputs\":[\"Start\",\"Stop\",\"Pause\"],\"outputs\":{\"Finished\":[{\"op\":\"S.F\",\"input_index\":0}],\"Aborted\":[{\"op\":\"S.A\",\"input_index\":0}]},\"variables\":{},\"incoming\":[{}]},"
            "{\"object\":\"S.F\",\"class\":\"X_Finished\",\"authored\":{},\"inputs\":[\"In\"],\"outputs\":{},\"variables\":{},\"incoming\":[{}]},"
            "{\"object\":\"S.A\",\"class\":\"X_Aborted\",\"authored\":{},\"inputs\":[\"In\"],\"outputs\":{},\"variables\":{},\"incoming\":[{}]}"));
        vm.remoteEvent("go");
        for (int k = 0; k < 60; ++k) vm.tick(dt);
        check(r.hits.size() == 1 && r.hits[0].second == "S.F" && std::fabs(r.hits[0].first - 0.5f) < 0.02f, "Delay 0.5 s: Finished at 0.5 s");
        r.hits.clear(); vm.remoteEvent("go");
        for (int k = 0; k < 6; ++k) vm.tick(dt);
        vm.impulse(vm.find("S.D"), 1); vm.tick(dt);
        check(r.hits.size() == 1 && r.hits[0].second == "S.A", "Delay Stop: Aborted, no Finished");
    }
    {   // Gate AutoCloseCount 1: one pass, then closed until Open
        game::KismetVM vm; Recorder r; vm.setHost(&r);
        vm.load(graph(
            "{\"object\":\"S.G\",\"class\":\"SeqAct_Gate\",\"authored\":{\"AutoCloseCount\":1},\"inputs\":[\"In\",\"Open\",\"Close\",\"Toggle\"],\"outputs\":{\"Out\":[{\"op\":\"S.O\",\"input_index\":0}]},\"variables\":{},\"incoming\":[]},"
            "{\"object\":\"S.O\",\"class\":\"X_Out\",\"authored\":{},\"inputs\":[\"In\"],\"outputs\":{},\"variables\":{},\"incoming\":[{}]}"));
        const int g = vm.find("S.G");
        vm.impulse(g, 0); vm.impulse(g, 0); vm.tick(dt);
        vm.impulse(g, 1); vm.impulse(g, 0); vm.tick(dt);
        check(r.hits.size() == 2, "Gate AutoCloseCount 1: passes once, closes, Open re-opens");
    }
    {   // RemoteEvent fan-out to two listeners; a base event (MaxTriggerCount 1) fires once; RemoteEvents are unlimited
        game::KismetVM vm; Recorder r; vm.setHost(&r);
        vm.load(graph(
            "{\"object\":\"S.R1\",\"class\":\"SeqEvent_RemoteEvent\",\"authored\":{\"EventName\":\"x\"},\"inputs\":[],\"outputs\":{\"Out\":[{\"op\":\"S.A\",\"input_index\":0}]},\"variables\":{},\"incoming\":[]},"
            "{\"object\":\"S.R2\",\"class\":\"SeqEvent_RemoteEvent\",\"authored\":{\"EventName\":\"x\"},\"inputs\":[],\"outputs\":{\"Out\":[{\"op\":\"S.B\",\"input_index\":0}]},\"variables\":{},\"incoming\":[]},"
            "{\"object\":\"S.T\",\"class\":\"SeqEvent_Touch\",\"authored\":{},\"inputs\":[],\"outputs\":{\"Out\":[{\"op\":\"S.C\",\"input_index\":0}]},\"variables\":{},\"incoming\":[]},"
            "{\"object\":\"S.A\",\"class\":\"X\",\"authored\":{},\"inputs\":[\"In\"],\"outputs\":{},\"variables\":{},\"incoming\":[{}]},"
            "{\"object\":\"S.B\",\"class\":\"X\",\"authored\":{},\"inputs\":[\"In\"],\"outputs\":{},\"variables\":{},\"incoming\":[{}]},"
            "{\"object\":\"S.C\",\"class\":\"X\",\"authored\":{},\"inputs\":[\"In\"],\"outputs\":{},\"variables\":{},\"incoming\":[{}]}"));
        vm.remoteEvent("x"); vm.remoteEvent("x");
        vm.fireEvent(vm.find("S.T")); vm.fireEvent(vm.find("S.T"));
        vm.tick(dt);
        check(r.hits.size() == 5, "RemoteEvent fans out to every listener, unlimited; a base event fires once (MaxTriggerCount 1)");
    }
    {   // SeqCond_Increment A += 1 vs B; a link into a sub-sequence fires its SequenceActivated
        game::KismetVM vm; Recorder r; vm.setHost(&r);
        vm.load(graph(
            "{\"object\":\"S.VA\",\"class\":\"SeqVar_Int\",\"authored\":{\"IntValue\":0},\"inputs\":[],\"outputs\":{},\"variables\":{},\"incoming\":[]},"
            "{\"object\":\"S.VB\",\"class\":\"SeqVar_Int\",\"authored\":{\"IntValue\":2},\"inputs\":[],\"outputs\":{},\"variables\":{},\"incoming\":[]},"
            "{\"object\":\"S.I\",\"class\":\"SeqCond_Increment\",\"authored\":{},\"inputs\":[\"In\"],\"outputs\":{\"A == B\":[{\"op\":\"S.Sub\",\"input_index\":0}]},\"variables\":{\"A\":[{\"variable\":\"S.VA\"}],\"B\":[{\"variable\":\"S.VB\"}]},\"incoming\":[]},"
            "{\"object\":\"S.Sub\",\"class\":\"Sequence\",\"authored\":{},\"inputs\":[],\"outputs\":{},\"variables\":{},\"incoming\":[{}]},"
            "{\"object\":\"S.Sub.In\",\"class\":\"SeqEvent_SequenceActivated\",\"authored\":{\"InputLabel\":\"In\"},\"inputs\":[],\"outputs\":{\"Out\":[{\"op\":\"S.Sub.X\",\"input_index\":0}]},\"variables\":{},\"incoming\":[]},"
            "{\"object\":\"S.Sub.X\",\"class\":\"X\",\"authored\":{},\"inputs\":[\"In\"],\"outputs\":{},\"variables\":{},\"incoming\":[{}]}"));
        const int i = vm.find("S.I");
        vm.impulse(i, 0); vm.tick(dt);
        check(r.hits.empty(), "Increment: A 1 < B 2, no A == B");
        vm.impulse(i, 0); vm.tick(dt);
        check(r.hits.size() == 1 && r.hits[0].second == "S.Sub.X", "Increment: A 2 == B 2 fires into the sub-sequence's SequenceActivated");
    }
    {   // D5_OME's exported graph
        std::ifstream f(kismetManifestRoot() + "/next_map/D5_OME_kismet.json", std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        assets::Json j;
        const bool ok = f.good() && assets::Json::parse(ss.str(), j);
        game::KismetVM vm;
        struct Stub : game::KismetVM::Host {
            std::vector<std::string> checkpoints; int conversations = 0, held = 0;
            bool activate(game::KismetVM& vm, int op, int input) override {
                const std::string& c = vm.op(op).cls;
                if (c == "SeqAct_SetActiveCheckpoint") { checkpoints.push_back(vm.objectOf(op, "Checkpoint")); vm.output(op, "Out"); return true; }
                // Latent classes (RE KH timing summary) are held by the stub (no outputs), so the test does not invent completions:
                if (c == "SeqAct_Interp" || c == "SeqAct_TnPawnFactory" || c == "SeqAct_MoviePlayer" || c == "TnSeqAct_MissionComplete" ||
                    c == "SeqAct_TnAiMoveToActor" || c == "SeqAct_SetStreamingState" || c == "SeqAct_TnForceIntoForm") { ++held; return true; }
                if (c == "SeqAct_Conversation" && input == 0) { ++conversations; vm.op(op).latent = true; vm.op(op).timer = 0.5f; vm.output(op, "Played"); return true; }
                return false;
            }
            void updateLatent(game::KismetVM& vm, int op, float dt) override {
                game::KismetVM::Op& o = vm.op(op);
                if (o.cls == "SeqAct_Conversation" && (o.timer -= dt) <= 0.0f) vm.finishLatent(op, "Finished");
            }
        } stub;
        vm.setHost(&stub);
        check(ok && vm.load(j) && vm.opCount() > 800, "D5_OME graph loads (ops and links from the flat per-level lists)");
        vm.begin("Checkpoint_4578");
        bool cp = false; for (const std::string& c : stub.checkpoints) cp = cp || c.find("Checkpoint_4578") != std::string::npos;
        check(cp, "D5 start at Checkpoint_4578: PreGameplaySetup sets it active");
        for (int k = 0; k < 120 * 60; ++k) vm.tick(dt);
        int total = 0; for (const auto& a : vm.activations) total += a.second;
        check(vm.queueCapHits == 0 && total > 0, "120 s of D5 ticks with no impulse-loop cap hit");
        std::string unhandled;
        for (const auto& d : vm.defaulted) unhandled += " " + d.first + "x" + std::to_string(d.second);
        LOG_INFO("KISMETTEST D5: %zu ops, %d activations, %zu classes, conversations %d, checkpoints set %zu; host-unhandled (generic Out):%s",
                 vm.opCount(), total, vm.activations.size(), stub.conversations, stub.checkpoints.size(), unhandled.c_str());
    }
    LOG_INFO("KISMETTEST SUMMARY: %d/%d checks passed", checks - fails, checks);
    exitCode_ = fails ? 1 : 0;
}

} // namespace core
