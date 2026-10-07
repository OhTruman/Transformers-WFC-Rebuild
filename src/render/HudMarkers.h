#pragma once
// Clean-room reconstruction — TnHUD Canvas objective / player markers (TnObjectiveMarkerTypeSprite.Draw).
//
// RE MILESTONE05 GAMEPLAY §5 / BLOCKERS §H: gameplay markers are Canvas material tiles (UI_HudMarkers_p) plus a
// MarkerFont label, not Scaleform. The authored marker-type setups come from render data (_ui/hud_markers.json,
// tools/render/build_hud.py). Gameplay decides WHICH markers exist and their rule-driven visibility (self / dead
// hidden, enemy tags only with the see-enemy buffs, line of sight); this class only draws them by the authored rules.
#include <map>
#include <string>
#include <vector>
#include "core/Math.h"
#include "render/Camera.h"
#include "render/Renderer.h"

namespace render {

struct MarkerRequest {
    std::string key;            // stable identity (focus hysteresis)
    std::string type;           // marker class, e.g. "TnObjectiveMarkerTypeTransformerVersus"
    std::string setup;          // setup property, e.g. "AllyMarkerSetup" / "EnemyMarkerSetup"
    core::Vec3 base;            // MarkerBase.Location (glTF metres)
    float labelZ = 0.0f;        // LabelZOffsetWS override in metres (Versus: collision height * 0.5); < 0 = authored
    std::string label;          // PRI.PlayerName / authored label
    bool drawHealthBar = false; // ally health bar (viewer specialty Scientist)
    float health = 1.0f;        // 0..1 for the health bar material
    std::vector<std::pair<std::string, std::array<float, 4>>> params;   // extra material params (Neutral, Flashing...)
    // Inputs for the authored fields still being implemented (RE semantics pending; accepted and ignored until then):
    std::string action;         // label key: "Attack" / "Capture" / "Defend" / "Defuse" / "Escort" / "Kill" / "Plant" /
                                // "Return" / "Idle" / "DownedEnemy" -> the type's authored <action>Label
    float pulseT = -1.0f;       // seconds since the marker's pulse started (PulseFrequency / PulseExpPower); < 0 none
    bool removing = false;      // the objective is gone: FadeOutTime runs from removedT
    float removedT = 0.0f;      // seconds since removal
    int relation = -1;          // -1 from the setup, 0 friendly, 1 enemy, 2 neutral (Friendly / Enemy / NeutralLabelColor)
};

class HudMarkers {
public:
    bool load(const std::string& renderDataRoot);   // <root>/_ui/hud_markers.json
    bool loaded() const { return !types_.empty(); }
    // Draw this frame's markers. dt drives the focus hysteresis.
    void draw(IRenderer& r, const Camera& cam, int width, int height, const std::vector<MarkerRequest>& markers, float dt);

private:
    struct Setup {
        std::string mat, healthMat, focusParam = "Over", onScreenParam = "OnScreen", rotationParam = "ArrowAngle";
        float zOffset = 0, labelZOffset = 0, focusedSize = 0.05f, unfocusedSize = 0.05f, offscreenSize = 0.0625f;
        float labelOffX = 0, labelOffY = 0;
        bool labelFocused = true, labelUnfocused = false, labelOffscreen = false, drawOffscreen = false;
        uint8_t labelColor[4] = {255, 255, 255, 255};
    };
    struct Type {
        std::map<std::string, Setup> setups;
        float focusThreshold = 0.0625f, focusHysteresis = 0.2f, autoFocusRange = 0.0f, safeFrame = 0.08f;
        float healthBarW = 0.06f, healthBarH = 0.015f, healthBarOffY = 0.02f;
        std::string font = "MarkerFont";
    };
    std::map<std::string, Type> types_;
    std::map<std::string, float> focusHold_;        // key -> remaining focus hold (s)
};

} // namespace render
