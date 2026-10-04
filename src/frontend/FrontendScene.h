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
};

// Implemented by Rendering: draws the scene levels with the frontend's camera before the GFx overlay.
class IFrontendSceneRenderer {
public:
    virtual ~IFrontendSceneRenderer() = default;
    virtual bool load(const std::vector<std::string>& levels) = 0;   // false: not exported / not drawable
    virtual void draw(const SceneView& view, int width, int height) = 0;
    virtual void unload() = 0;
};

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

    // Evaluation helpers (exposed for the headless tests).
    struct Key { double t = 0; double v[3] = {0, 0, 0}, ai[3] = {0, 0, 0}, lo[3] = {0, 0, 0}; int mode = 0; };   // 0 linear 1 constant 2 curve
    static void evalCurve(const std::vector<Key>& keys, double t, double out[3]);

private:
    struct MoveTrack { bool relativeToInitial = false; std::vector<Key> pos, euler; };
    struct Group { std::string name; bool director = false; std::vector<std::string> actors; std::vector<MoveTrack> moves;
                   std::vector<std::pair<double, std::string>> cuts; };
    struct Matinee { std::string name, comment; bool looping = false; double length = 0; std::vector<Group> groups;
                     std::vector<std::string> fscommands; bool onMovieStopped = false; };
    struct Actor { std::string name, cls; double loc[3] = {0, 0, 0}, rot[3] = {0, 0, 0}; std::string base;
                   double relLoc[3] = {0, 0, 0}, relRot[3] = {0, 0, 0}; double fov = 0; bool camera = false; };
    struct Level { std::vector<Matinee> matinees; std::map<std::string, Actor> actors; };
    struct Playing { const Matinee* m; double t; int order; };

    void start(const Matinee& m);
    // Actor world transform with the playing matinees' tracks applied (UE rotation matrix rows = X / Y / Z axes).
    void actorWorld(const std::string& name, double pos[3], double rotM[9], int depth = 0) const;
    const Actor* findActor(const std::string& name) const;

    bool loaded_ = false;
    std::map<std::string, std::vector<std::string>> scenes_;   // UI level -> scene levels
    std::map<std::string, Level> data_;
    std::vector<std::string> levels_;
    std::vector<Playing> playing_;
    int order_ = 0;
    double time_ = 0.0;
};

} // namespace frontend
