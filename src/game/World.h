// Clean-room reconstruction — the world: owns the level, player, and dynamic actors.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/Math.h"
#include "game/Player.h"
#include "game/Pickup.h"
#include "game/SpawnPoint.h"
#include "game/Collision.h"
#include "render/Mesh.h"
#include "assets/SkinnedModel.h"
#include "audio/Audio.h"
#include "game/WeaponMesh.h"

namespace render { class IRenderer; }

namespace game {

// A static graybox block (fallback level + future collision volume).
struct Block {
    core::Vec3 center;
    core::Vec3 size;
    core::Vec3 color;
};

class World {
public:
    // Uploads meshes via the renderer. Tries the extracted vertical slice first; if any
    // required asset is missing, falls back to the procedural graybox arena.
    void load(render::IRenderer& renderer);

    void tick(float dt);                       // one fixed step
    void handleInput(const platform::InputFrame& in, float dt);
    void draw(render::IRenderer& r) const;

    // Instant-hit weapon trace from `origin` along `dir` (uses the pawn's weapon for
    // spread, range and range-based damage falloff). Damages the nearest target.
    void fireHitscan(const core::Vec3& origin, const core::Vec3& dir);

    // Audio: wired by the application; World loads cues and plays them on gameplay events.
    enum class Sfx { Fire, Reload, Transform, Land };
    void setAudio(audio::IAudio* a);
    void playSfx(Sfx s, const core::Vec3& pos);

    Player& player() { return player_; }
    bool usingSlice() const { return usingSlice_; }

    // Collision for queries by movement; null when none is loaded (graybox fallback).
    const CollisionWorld* collision() const { return collision_.valid() ? &collision_ : nullptr; }

private:
    bool loadVerticalSlice(render::IRenderer& renderer);
    void buildGraybox();
    bool loadSpawn(const std::string& spawnJsonPath, core::Vec3& outPos, float& outYaw);
    void respawnPlayer();

    Player player_;
    std::vector<Block> blocks_;
    std::vector<std::unique_ptr<Actor>> actors_;
    std::vector<SpawnPoint> spawns_;
    CollisionWorld collision_;

    core::Vec3 spawnPos_{0, 0, 0};
    float spawnYaw_ = 0.0f;
    float killZ_ = -1e9f;   // respawn if player falls below this Y

    render::MeshHandle mapMesh_ = render::kInvalidMesh;
    render::MeshHandle weaponMesh_ = render::kInvalidMesh;
    core::Vec3 mapColor_{0.55f, 0.57f, 0.6f};
    bool usingSlice_ = false;

    assets::SkinnedModel robotModel_;
    assets::SkinnedModel vehicleModel_;

    // Ion Blaster as an animated skeletal mesh (own Fire/Reload/Idle anims, sockets, notifies).
    assets::SkinnedModel weaponModel_;
    WeaponMesh weaponAnim_;
    unsigned weaponSeenShot_ = 0, weaponSeenReload_ = 0;
    std::vector<WeaponNotify> notifies_;
    void tickWeaponPresentation(float dt);
    void handleWeaponNotify(const WeaponNotify& n);
    // World transform of a weapon socket (MuzzleFlash/ShellSocket/MagSocket); false if unavailable.
    bool weaponSocketWorld(const char* socket, core::Mat4& out) const;

    // Transient weapon-fire effects (tracers + muzzle flash).
    struct Shot { core::Vec3 a, b; float ttl; };
    std::vector<Shot> shots_;

    audio::IAudio* audio_ = nullptr;
    audio::Sound sndFire_ = audio::kInvalidSound, sndReload_ = audio::kInvalidSound,
                 sndTransform_ = audio::kInvalidSound, sndLand_ = audio::kInvalidSound;
    bool prevGrounded_ = true;
    bool prevTransforming_ = false;
    bool prevReloading_ = false;
};

} // namespace game
