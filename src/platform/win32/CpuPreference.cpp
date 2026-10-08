#include "platform/CpuPreference.h"
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00                 // CPU set types (Windows 10); the functions are looked up at run time, so the exe
#endif                                      // still loads on older Windows (which simply gets no preference)
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace platform {

namespace {
struct L3 { unsigned long kb; WORD group; KAFFINITY mask; };
using GetCpuSetInfoFn = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
using SetDefaultCpuSetsFn = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
template <class F> F k32(const char* name) { return (F)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), name); }

std::vector<L3> l3Domains() {
    std::vector<L3> out;
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &len);
    if (len == 0) return out;
    std::vector<char> buf(len);
    if (!GetLogicalProcessorInformationEx(RelationCache, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data(), &len)) return out;
    for (DWORD off = 0; off < len;) {
        auto* e = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf.data() + off);
        if (e->Relationship == RelationCache && e->Cache.Level == 3)
            out.push_back({e->Cache.CacheSize / 1024, e->Cache.GroupMask.Group, e->Cache.GroupMask.Mask});
        off += e->Size;
    }
    return out;
}

// CPU set ids of the logical processors in (group, mask).
std::vector<ULONG> cpuSetIds(WORD group, KAFFINITY mask) {
    std::vector<ULONG> ids;
    const auto getInfo = k32<GetCpuSetInfoFn>("GetSystemCpuSetInformation");
    if (!getInfo) return ids;
    ULONG len = 0;
    getInfo(nullptr, 0, &len, GetCurrentProcess(), 0);
    if (len == 0) return ids;
    std::vector<char> buf(len);
    if (!getInfo((PSYSTEM_CPU_SET_INFORMATION)buf.data(), len, &len, GetCurrentProcess(), 0)) return ids;
    for (ULONG off = 0; off < len;) {
        auto* e = (PSYSTEM_CPU_SET_INFORMATION)(buf.data() + off);
        if (e->Type == CpuSetInformation && e->CpuSet.Group == group && e->CpuSet.LogicalProcessorIndex < 64 &&
            (mask & ((KAFFINITY)1 << e->CpuSet.LogicalProcessorIndex)))
            ids.push_back(e->CpuSet.Id);
        off += e->Size;
    }
    return ids;
}
} // namespace

std::string applyCachePreference() {
    const char* env = std::getenv("WFC_CPUSETS");
    const std::string mode = env ? env : "auto";
    if (mode == "off") return "cpu sets: off (WFC_CPUSETS=off)";
    const std::vector<L3> l3 = l3Domains();
    if (l3.size() < 2) return "cpu sets: one L3 domain - nothing to prefer";
    int pick = -1;
    if (mode == "ccd0" || mode == "ccd1") pick = mode == "ccd0" ? 0 : 1;
    else {
        size_t best = 0;
        bool asym = false;
        for (size_t i = 1; i < l3.size(); ++i) {
            if (l3[i].kb != l3[0].kb) asym = true;
            if (l3[i].kb > l3[best].kb) best = i;
        }
        if (!asym) return "cpu sets: symmetric L3 - left to the scheduler";
        pick = (int)best;
    }
    if (pick < 0 || (size_t)pick >= l3.size()) return "cpu sets: no such L3 domain";
    const std::vector<ULONG> ids = cpuSetIds(l3[(size_t)pick].group, l3[(size_t)pick].mask);
    const auto setDefault = k32<SetDefaultCpuSetsFn>("SetProcessDefaultCpuSets");
    if (!setDefault) return "cpu sets: not supported by this Windows";
    if (ids.empty() || !setDefault(GetCurrentProcess(), ids.data(), (ULONG)ids.size()))
        return "cpu sets: SetProcessDefaultCpuSets failed";
    char b[160];
    std::snprintf(b, sizeof b, "cpu sets: preferring L3 domain %d (%lu KB, group %u mask 0x%llx, %zu logical processors)%s", pick,
                  l3[(size_t)pick].kb, l3[(size_t)pick].group, (unsigned long long)l3[(size_t)pick].mask, ids.size(),
                  mode == "auto" ? " - the largest L3" : " (forced)");
    return b;
}

} // namespace platform
