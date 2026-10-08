#pragma once
// PC ADAPTATION (300 fps): on CPUs whose L3 differs between core complexes (AMD X3D parts with one 3D V-cache CCD), prefer the
// cores of the largest L3 for every thread of the process (SetProcessDefaultCpuSets - a preference the scheduler honours,
// not a hard affinity). CPUs with symmetric L3 are left alone. WFC_CPUSETS=off | auto (default) | ccd0 | ccd1 overrides
// (ccdN = the N-th L3 domain in system order, for A/B). Call once at startup, before worker threads start; returns a
// one-line description of what was done (for the log).
#include <string>

namespace platform {
std::string applyCachePreference();
}
