// Clean-room reconstruction — cooperative presentation during synchronous loads.
//
// The shipped game streams levels behind TnMoviePlayer's loading movie, which keeps animating while the level loads
// (the movie runs on its own thread natively). The rebuild loads a map synchronously on the main thread, so long load
// loops (world data, map render data, level audio) call loadYield() between complete steps. When the application has
// registered a presenter, loadYield() runs it at most once per frame interval: OS messages, the loading movie's
// animation, its Bink underlay and one presented frame. Nothing else runs inside it (no flow transitions, no game
// tick), and it never runs in the middle of a GL upload because callers only yield between whole operations.
#pragma once
#include <chrono>
#include <functional>
#include <string>

namespace core {

using LoadYieldFn = std::function<void(double dtSeconds)>;

void setLoadYield(LoadYieldFn fn);   // nullptr = no presentation (direct boot, tests)
void loadYield(const char* where);   // cheap when not due / not registered

// Diagnostics of the current load: the longest gap between presented frames and where it ended.
struct LoadYieldStats { double maxGapMs = 0.0; const char* maxGapAt = ""; int frames = 0; };
void resetLoadYieldStats();
LoadYieldStats loadYieldStats();

// Loading progress from the loader's own steps: every loadYield call is one step. beginLoadProgress(key) at a load's start
// (key = map + mode: the step total of its last load comes from wfc_loadprofile.ini), endLoadProgress() at its end (the
// total recorded). fraction: steps / the last total (capped at 0.99 until the end), a first load eases toward 0.9;
// never decreasing. stage: the current step's label ("World: texture", "Render: load step", ...).
struct LoadProgress { bool active = false; float fraction = 0.0f; const char* stage = ""; long steps = 0, expected = 0; };
void beginLoadProgress(const std::string& key);
void endLoadProgress();
LoadProgress loadProgress();

// DIAGNOSTIC (WFC_LOADSTAGES=1, Systems' load-time pass): the wall time between consecutive loadYield calls, summed per stage label
// (the work that ended at that step), and logLoadStages() prints the table: the top labels, their share, the step counts.
void logLoadStages(const char* when);
void addLoadTime(const char* what, double ms);   // a measured part inside the steps (e.g. "texture decode"), listed by logLoadStages
struct LoadPartTimer {   // scope -> addLoadTime(what, its wall time)
    const char* what; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    explicit LoadPartTimer(const char* w) : what(w) {}
    ~LoadPartTimer() { addLoadTime(what, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); }
};

} // namespace core
