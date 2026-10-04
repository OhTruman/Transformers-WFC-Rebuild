// Clean-room reconstruction — cooperative presentation during synchronous loads.
//
// The shipped game streams levels behind TnMoviePlayer's loading movie, which keeps animating while the level loads
// (the movie runs on its own thread natively). The rebuild loads a map synchronously on the main thread, so long load
// loops (world data, map render data, level audio) call loadYield() between complete steps. When the application has
// registered a presenter, loadYield() runs it at most once per frame interval: OS messages, the loading movie's
// animation, its Bink underlay and one presented frame. Nothing else runs inside it (no flow transitions, no game
// tick), and it never runs in the middle of a GL upload because callers only yield between whole operations.
#pragma once
#include <functional>

namespace core {

using LoadYieldFn = std::function<void(double dtSeconds)>;

void setLoadYield(LoadYieldFn fn);   // nullptr = no presentation (direct boot, tests)
void loadYield(const char* where);   // cheap when not due / not registered

// Diagnostics of the current load: the longest gap between presented frames and where it ended.
struct LoadYieldStats { double maxGapMs = 0.0; const char* maxGapAt = ""; int frames = 0; };
void resetLoadYieldStats();
LoadYieldStats loadYieldStats();

} // namespace core
