// Clean-room reconstruction — the live 3D level under the frontend menus (Kismet / matinee / camera side).
//
// The shipped menus are an overlay on running levels (RE OVERNIGHT_2026-10-04 section D, CONFIRMED): UI_FrontEnd_m
// (+ streamed UI_FrontEnd_capture_VIG_m) under the title and main menu, UI_CharacterCustomization_m under the party and
// game lobbies, UI_CampaignLobby_m under the campaign / escalation lobby. This module owns what the frontend drives in
// those levels: which levels make up the scene, the Kismet triggers (fscommands of the menu movies, the intro movie's
// Stopped output), the SeqAct_Interp matinees they start, and the camera they produce (authored CameraActor pose and
// FOV, director cuts, move tracks, hard attachment). Data: data/frontend/scenes.json (tools/frontend/
// export_frontend_scenes.py, from authored.db). Drawing the levels is Rendering's (IFrontendSceneRenderer).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace frontend {

// The camera the scene renders with, in Unreal units / degrees (UE axes: X forward, Y right, Z up).
struct SceneView {
    bool valid = false;
    std::vector<std::string> levels;   // scene levels (persistent first)
    double pos[3] = {0, 0, 0};
    double rot[3] = {0, 0, 0};          // pitch, yaw, roll (degrees)
    double fov = 90.0;                  // horizontal FOV (CameraActor.FOVAngle)
    std::string camera;                 // CameraActor name
    std::string matinee;                // the matinee that cuts to / moves the camera ("" = authored pose)
    double time = 0.0;                  // seconds since the scene started (effects / emitters)
    // Actors moved by the playing matinees (InterpActors, skeletal ships, emblems): absolute world pose, UE units /
    // degrees, RelativeToInitial and hard attachment applied. Actors not listed keep their authored pose.
    struct ActorPose { std::string actor; double pos[3]; double rot[3]; };
    std::vector<ActorPose> actors;
    // Matinee DrawScale tracks (InterpTrackFloatProp): absolute DrawScale per actor (vignette ships / boosters).
    struct ActorScale { std::string actor; double drawScale; };
    std::vector<ActorScale> scales;
    // InterpTrackFloatMaterialParam: scalar parameters of MaterialInstanceActors (the faction emblems' Highlighted /
    // Opacity, driven by the movie's glow / dim / fadein / fadeout fscommands).
    struct MaterialParam { std::string actor, param; double value; };
    std::vector<MaterialParam> materialParams;
};

// Implemented by Rendering: draws the scene levels with the frontend's camera before the GFx overlay.
class IFrontendSceneRenderer {
public:
    virtual ~IFrontendSceneRenderer() = default;
    virtual bool load(const std::vector<std::string>& levels) = 0;   // false: not exported / not drawable
    virtual void draw(const SceneView& view, int width, int height) = 0;
    virtual void unload() = 0;
    // Kismet / matinee driven state of the scene's actors: emitter activation (InterpTrackToggle) and visibility
    // (SeqAct_ToggleHidden fired by matinee event keys).
    virtual void setEffectActive(const std::string& actor, bool on) { (void)actor; (void)on; }
    virtual void setActorHidden(const std::string& actor, bool hidden) { (void)actor; (void)hidden; }
};

struct SceneChange { enum Kind { Effect, Hidden } kind; std::string actor; bool value; };

class FrontendScene {
public:
    bool load(const std::string& path);
    bool loaded() const { return loaded_; }
    // A UI level began (travel finished): the scene's levels, their Kismet state reset.
    void enterLevel(const std::string& uiLevel);
    void leave();
    // Frontend-owned Kismet triggers, as GameFlow reports them: "FsCommand:<cmd>", "MovieStopped:<movie>".
    void trigger(const std::string& kismetTrigger);
    void tick(double dt);
    SceneView view() const;
    const std::vector<std::string>& levels() const { return levels_; }
    std::vector<std::string> playing() const;
    // Effect / visibility changes since the last call (matinee toggle and event keys crossed by tick()).
    std::vector<SceneChange> takeChanges() { std::vector<SceneChange> c; c.swap(changes_); return c; }
    // Customization camera: the customization camera id (TnDataProvider_Chassis.CustomizationCameraId) of the chassis
    // shown by preview slot 0 (Autobot) / 1 (Decepticon); -1 = none (SeqVar_TnCustomizationCameraId).
    std::function<int(int slot)> cameraIdForSlot;

    // Evaluation helpers (exposed for the headless tests).
    struct Key { double t = 0; double v[3] = {0, 0, 0}, ai[3] = {0, 0, 0}, lo[3] = {0, 0, 0}; int mode = 0; };   // 0 linear 1 constant 2 curve
    static void evalCurve(const std::vector<Key>& keys, double t, double out[3]);

private:
    struct MoveTrack { bool relativeToInitial = false; std::vector<Key> pos, euler; };
    struct Group { std::string name; bool director = false; std::vector<std::string> actors; std::vector<MoveTrack> moves;
                   std::vector<std::pair<double, std::string>> cuts;
                   std::vector<std::pair<double, int>> toggles;              // 0 off, 1 on, 2 toggle
                   std::vector<std::pair<double, std::string>> events;
                   std::vector<std::pair<std::string, std::vector<Key>>> floats;   // InterpTrackFloatProp (property, keys)
                   std::vector<std::pair<std::string, std::vector<Key>>> materialParams; };   // InterpTrackFloatMaterialParam
    struct EventAction { std::string event; int action = 2; std::vector<std::string> targets; };   // 0 hide 1 unhide 2 toggle
    struct Matinee { std::string name, comment; bool looping = false; double length = 0; std::vector<Group> groups;
                     std::vector<std::string> fscommands; bool onMovieStopped = false;
                     std::vector<std::string> reverseFscommands;   // fscommands that Reverse it (through subsequence inputs)
                     std::vector<std::string> remoteEvents;   // SeqEvent_RemoteEvent names that play it
                     std::vector<EventAction> eventActions;
                     struct SubStart { std::string sub, output; bool reverse = false; };
                     std::vector<SubStart> subStarts; };   // started by a subsequence's output (Play / Reverse)
    struct CameraSwitch { std::string name; int slot = 0; std::map<int, std::string> outputs; std::vector<std::string> triggers; };
    struct RemoteActivator { std::string event; std::vector<std::string> fscommands; bool onMovieStopped = false; };
    struct Actor { std::string name, cls; double loc[3] = {0, 0, 0}, rot[3] = {0, 0, 0}; std::string base;
                   double relLoc[3] = {0, 0, 0}, relRot[3] = {0, 0, 0}; double fov = 0; bool camera = false; };
    struct PawnVisibility { std::string fscommand; int action = 2; std::vector<std::string> pawns; };   // 0 hide 1 unhide 2 toggle
    struct Level { std::vector<Matinee> matinees; std::map<std::string, Actor> actors; std::vector<RemoteActivator> remotes;
                   std::vector<CameraSwitch> switches; std::vector<PawnVisibility> pawnVis; };
    void subOutput(const std::string& sub, const std::string& output);
    void remoteEvent(const std::string& name);
    struct Playing { const Matinee* m; double t; int order; double rate = 1.0; };   // rate -1: Reverse

    void start(const Matinee& m);
    // Actor world transform with the playing matinees' tracks applied (UE rotation matrix rows = X / Y / Z axes).
    void actorWorld(const std::string& name, double pos[3], double rotM[9], int depth = 0) const;
    const Actor* findActor(const std::string& name) const;

    bool loaded_ = false;
    std::map<std::string, std::vector<std::string>> scenes_;   // UI level -> scene levels
    std::map<std::string, Level> data_;
    std::vector<std::string> levels_;
    std::vector<Playing> playing_;
    std::vector<SceneChange> changes_;
    std::map<std::string, bool> effectOn_, hidden_;
    void crossKeys(const Matinee& m, double from, double to);
    int order_ = 0;
    double time_ = 0.0;
};

} // namespace frontend
