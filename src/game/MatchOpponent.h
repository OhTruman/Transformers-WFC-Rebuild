// Clean-room reconstruction — TEST / DIAGNOSTIC ONLY: a synthetic match participant (no AI, no WFC content).
// Exists only when a harness (WFC_MATCHTEST) or the explicit diagnostic flag WFC_MATCH_OPPONENTS creates it. It owns a
// Match player slot, spawns / dies through the real Match flow, stands at its start (or where the harness puts it)
// and takes Ion Blaster hitscan damage through the same team filter and segmented health as a player pawn
// (robot cylinder: radius 2 m, height 4 m). Drawn as a box only under the diagnostic flag.
#pragma once
#include "game/Actor.h"
#include "game/Health.h"
#include "render/Renderer.h"

namespace game {

class MatchOpponent : public Actor {
public:
    MatchOpponent(int matchPlayer, int team, bool drawn) : player_(matchPlayer), team_(team), drawn_(drawn) {}
    int matchPlayer() const { return player_; }
    int team() const { return team_; }
    Health& health() { return health_; }
    bool spawned() const { return spawned_; }
    void spawnAt(const core::Vec3& p) { pos_ = p; spawned_ = true; health_ = Health{}; }
    void despawn() { spawned_ = false; }
    void setPosition(const core::Vec3& p) { pos_ = p; }
    // Robot collision cylinder (feet at pos_).
    bool rayHit(const core::Vec3& o, const core::Vec3& d, float range, float& t) const {
        if (!spawned_) return false;
        const float r = 2.0f, h = 4.0f;
        float ox = o.x - pos_.x, oz = o.z - pos_.z;
        float a = d.x * d.x + d.z * d.z, b = 2.0f * (ox * d.x + oz * d.z), c = ox * ox + oz * oz - r * r;
        float tt;
        if (a < 1e-8f) { if (c > 0.0f) return false; tt = 0.0f; }
        else {
            float disc = b * b - 4.0f * a * c;
            if (disc < 0.0f) return false;
            tt = (-b - std::sqrt(disc)) / (2.0f * a);
            if (tt < 0.0f) tt = 0.0f;
        }
        if (tt > range) return false;
        float y = o.y + d.y * tt;
        if (y < pos_.y || y > pos_.y + h) return false;
        t = tt;
        return true;
    }
    void draw(render::IRenderer& r) const override {
        if (!drawn_ || !spawned_) return;
        core::Vec3 col = team_ == 0 ? core::Vec3{0.3f, 0.45f, 1.0f} : core::Vec3{1.0f, 0.3f, 0.25f};
        r.drawBox(pos_ + core::Vec3{0, 2.0f, 0}, core::Vec3{2.8f, 4.0f, 2.8f}, col);
    }

private:
    int player_, team_;
    bool drawn_;
    bool spawned_ = false;
    Health health_;
};

} // namespace game
