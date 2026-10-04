// wfc_gfxdump: runs one original GFx movie headlessly (AVM1 + display list) and prints its display tree.
// Usage: wfc_gfxdump <movie object, e.g. UI_GFxFrontEnd_p.FrontEnd_GFX_1> [frames=60] [keys="30:114,40:13"]
//        keys = frame:flashKeyCode pairs (down on that frame, up the next). Bridge calls go to a FrontendRuntime.
#include "frontend/FrontendRuntime.h"
#include "ui/GfxHost.h"
#include "core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

using namespace gfx::avm1;

static Value toValue(const frontend::BridgeValue& b) {
    switch (b.kind) {
    case frontend::BridgeValue::Kind::Bool: return Value(b.b);
    case frontend::BridgeValue::Kind::Number: return Value(b.n);
    case frontend::BridgeValue::Kind::String: return Value(b.s);
    default: return Value();
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: wfc_gfxdump <movie object> [frames] [keys]\n"); return 1; }
    std::string object = argv[1];
    int frames = argc > 2 ? std::atoi(argv[2]) : 60;
    std::map<int, int> keys;
    if (argc > 3) {
        std::stringstream ss(argv[3]); std::string item;
        while (std::getline(ss, item, ',')) { size_t c = item.find(':'); if (c != std::string::npos) keys[std::atoi(item.c_str())] = std::atoi(item.c_str() + c + 1); }
    }
    frontend::FrontendRuntime rt;
    if (!rt.init()) return 2;
    if (const char* pre = std::getenv("WFC_DUMP_PRESCRIPT")) {
        // Drive the flow (bridge calls) first, e.g. "call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby".
        rt.script().load(pre);
        platform::InputFrame none;
        for (int i = 0; i < 6000 && !rt.scriptFinished(); ++i) rt.update(none, 1.0f / 60.0f);
        std::printf("PRESCRIPT done: %s\n", rt.flow().stateSummary().c_str());
    }
    ui::GfxLibrary lib;
    if (!lib.load(frontend::Catalog::defaultManifestRoot(), frontend::Catalog::defaultExtractedRoot())) return 3;
    ui::GfxMovie movie;
    bool ok = movie.open(lib, &rt.catalog(), object,
        [&rt](ui::GfxMovie& m, const std::string& fn, Args& a) -> Value {
            std::vector<std::string> sa;
            for (auto& v : a) sa.push_back(m.player().vm().toString(v));
            frontend::BridgeValue r = rt.bridge(m.object(), fn, sa);
            std::printf("BRIDGE %s(%zu args) -> %s\n", fn.c_str(), sa.size(), r.str().c_str());
            return toValue(r);
        },
        [](ui::GfxMovie&, const std::string& c, const std::string& a) { std::printf("FSCOMMAND %s %s\n", c.c_str(), a.c_str()); });
    if (!ok) return 4;
    int pendingUp = -1;
    for (int f = 0; f < frames; ++f) {
        if (pendingUp >= 0) { movie.key(pendingUp, false); pendingUp = -1; }
        auto k = keys.find(f);
        if (k != keys.end()) { std::printf("KEY %d down (frame %d)\n", k->second, f); movie.key(k->second, true); pendingUp = k->second; }
        movie.advance(1.0f / movie.frameRate());
    }
    std::printf("%s", movie.dumpTree().c_str());
    return 0;
}
