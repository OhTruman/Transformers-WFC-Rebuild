// Systems M08 probe (not part of the CMake build): WeaponFx's seam to Rendering's generic particle runtime, bound to a
// recording fake. Checks: reconstructed templates never reach the runtime; others spawn with the documented frame
// (muzzle: socket X forward / Z up, impact: +X = surface normal, tracer: start -> end); a muzzle effect follows the
// socket while it lives; an unknown template (-1) is logged once and not drawn.
#include "game/WeaponFx.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { const bool ok_ = (c); std::printf("  %s ", ok_ ? "ok  " : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); if (!ok_) ++g_fail; } while (0)

int main() {
    using core::Vec3;
    struct Call { std::string kind, tpl; Vec3 a, b, c; int h; };
    std::vector<Call> calls;
    int next = 0;
    game::WeaponFx fx;
    game::WeaponFx::GenericRuntime g;
    g.spawnAt = [&](const std::string& t, const Vec3& p, const Vec3& f, const Vec3& u) {
        if (t.find("Bogus") != std::string::npos) return -1;
        calls.push_back({"at", t, p, f, u, next}); return next++;
    };
    g.spawnSegment = [&](const std::string& t, const Vec3& a, const Vec3& b) { calls.push_back({"seg", t, a, b, {}, next}); return next++; };
    g.setTransform = [&](int h, const Vec3& p, const Vec3& f, const Vec3& u) { calls.push_back({"move", "", p, f, u, h}); return true; };
    fx.setGenericRuntime(g);

    core::Mat4 sock = core::Mat4::identity();           // socket: X forward (1,0,0), Z up (0,0,1), at (1,2,3)
    sock.m[12] = 1; sock.m[13] = 2; sock.m[14] = 3;
    CHECK(fx.spawnMuzzleFlash(game::WeaponFx::kMuzzleFlashTemplate, sock) && calls.empty(), "reconstructed muzzle: drawn here, not sent");
    CHECK(fx.spawnMuzzleFlash("FX_EMPShotgun_p.FX.MuzzleFlash_EMPShotgun_FX", sock) && calls.size() == 1 && calls[0].kind == "at" &&
          calls[0].a.x == 1 && calls[0].a.z == 3 && calls[0].b.x == 1 && calls[0].c.z == 1, "other muzzle: spawnAt(socket pos, X fwd, Z up)");
    const bool imp = fx.spawnImpact("FX_EMPShotgun_p.FX.Squib_EMPShotgun_FX", {5, 0, 0}, {0, 1, 0}, {0, 0, 0});
    CHECK(imp && calls.size() == 2 &&
          std::fabs(calls[1].b.y - 1.0f) < 1e-5f && std::fabs(core::dot(calls[1].b, calls[1].c)) < 1e-5f, "impact: +X = normal, up perpendicular");
    CHECK(fx.spawnTracer("FX_EMPShotgun_p.FX.Trail_EMPShotgun_FX", {0, 0, 0}, {0, 0, 10}) && calls.size() == 3 && calls[2].kind == "seg" &&
          calls[2].b.z == 10, "tracer: spawnSegment(start, end)");
    core::Mat4 moved = sock; moved.m[12] = 4;
    fx.tick(0.1f, &moved);
    CHECK(calls.size() == 4 && calls[3].kind == "move" && calls[3].h == calls[0].h && calls[3].a.x == 4, "the muzzle effect follows the socket");
    fx.tick(2.5f, &moved);
    const size_t n = calls.size();
    fx.tick(0.1f, &moved);
    CHECK(calls.size() == n, "and stops following after its lifetime");
    CHECK(!fx.spawnImpact("FX_Bogus_p.FX.Bogus_FX", {0, 0, 0}, {0, 1, 0}, {0, 0, 0}) && !fx.spawnImpact("FX_Bogus_p.FX.Bogus_FX", {0, 0, 0}, {0, 1, 0}, {0, 0, 0}) &&
          fx.missingTemplates().count("FX_Bogus_p.FX.Bogus_FX") == 1 && fx.genericSpawned() == 3, "runtime -1: not drawn, logged once");
    std::printf("%s\n", g_fail ? "FAIL" : "OK");
    return g_fail ? 1 : 0;
}
