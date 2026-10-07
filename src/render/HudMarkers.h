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
    // RE 7bb8ec1 (notes/MILESTONE_E_OBJECTIVE_MARKERS.md):
    std::string action;         // the type's authored <action>Label as the label text when `label` is empty: "Capture" /
                                // "Defend" / "Return" / "Escort" / "Kill" / "Bomb" / "Plant" / "Defuse" / "Revive" /
                                // "Attack" / "Idle" / "DownedEnemy" (flag: home Capture|Defend, dropped Capture|Return,
                                // teammate-carried Escort, enemy-carried Kill; bomb Bomb|Escort|Kill; plant point
                                // Plant|Defuse|Defend; DOM / KOTH own Defend else Capture)
    float pulseT = -1.0f;       // seconds since the pulse started (only while an ENEMY carries the flag / bomb); < 0 none:
                                // pulse = ((2.5 - t mod 2.5) / 2.5)^5 -> material param PingOpacity and the label alpha
    float lifeSpan = -1.0f;     // remaining LifeSpan (s) for FadeOutTime types (Tombstone): Alpha = LifeSpan / FadeOutTime
    int owner = -1;             // the pawn's draw owner (IRenderer::setDrawOwner): EnemyMarkerHysterisis types show only
                                // while that pawn's mesh was rendered within the hysteresis (Mesh.LastRenderTime)
    int relation = -1;          // -1 the setup's LabelColor, 0 friendly, 1 enemy, 2 neutral (Friendly / Enemy / Neutral)
    bool removing = false;      // (kept for callers; no generic fade on removal - RE)
    float removedT = 0.0f;
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
        float drawCutoff = 0.0f;           // DrawCutoffDistance (UU), 0 = none: hidden beyond, arrow included
        float fadeOutTime = 0.0f;          // FadeOutTime (s)
        bool fillBlue = false;             // FillColor == HBFC_Blue -> health bar param Neutral = 1
        float enemyHysteresis = 0.0f;      // EnemyMarkerHysterisis (s)
        std::map<std::string, std::string> labels;   // action -> authored <action>Label
        uint8_t friendlyColor[4] = {80, 181, 213, 255}, enemyColor[4] = {240, 60, 60, 255}, neutralColor[4] = {255, 255, 255, 255};
    };
    std::map<std::string, Type> types_;
    std::map<std::string, float> focusHold_;        // key -> remaining focus hold (s)
};

} // namespace render
