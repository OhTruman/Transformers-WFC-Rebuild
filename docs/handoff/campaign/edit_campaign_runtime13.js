// Campaign runtime step 13 (anchor edits; on RUNTIME12): matinee AnimControl on pawn groups (CHARACTER:<ROBODEF> bindings from
// AssetTools matinee_skeletal.json pawn_groups: A1's lift ride trio) through a forced-clip slot on Character (setCineClip /
// clearCineClip: the robot-form base pose sampled from that clip while a matinee drives it). WorldCampaignMatinee.cpp copied from argv[2].
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignMatinee.cpp'), 'src/game/WorldCampaignMatinee.cpp');
ed('src/game/Campaign.h', [
    ['    float length = 0.0f;\n    std::vector<Group> groups;\n};',
     '    float length = 0.0f;\n    std::vector<Group> groups;\n    std::string interpData;   // the InterpData path (the pawn-group key)\n};\n' +
     '// A matinee group bound to characters at runtime (CHARACTER:<ROBODEF>): their chassis and the anim file of each clip.\n' +
     'struct CampaignPawnGroup { std::vector<std::string> chassis; std::map<std::string, std::string> clipFile; };'],
    ['bool viewing = false; float lastFade = 0.0f; };', 'bool viewing = false; float lastFade = 0.0f; std::vector<int> cinePawns; };'],
]);
ed('src/game/World.h', [
    ['    void cpLoadSkeletal();\n',
     '    void cpLoadSkeletal();\n' +
     '    void cpLoadPawnGroups();\n' +
     '    void cpPlayPawnClip(CampaignState::Interp& in, const CampaignPawnGroup& g, const std::string& clip, float time, bool loop);\n' +
     '    std::map<std::string, CampaignPawnGroup> cpPawnGroups_;   // "<InterpData>|<group>" -> its characters\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadSkeletal();\n', '        cpLoadSkeletal();\n        cpLoadPawnGroups();\n'],
]);
ed('src/game/Character.h', [
    ['    void playClip(const assets::SkinnedModel& mdl, int clip, bool loop, float dt, float blendOnChange);\n',
     '    void playClip(const assets::SkinnedModel& mdl, int clip, bool loop, float dt, float blendOnChange);\n' +
     '    int cineClip_ = -1; float cineTime_ = 0.0f; bool cineLoop_ = false;   // a campaign matinee AnimControl (robot-model clip)\n'],
    ['    void beginTransform();\n',
     '    void beginTransform();\n' +
     '    // Campaign matinee AnimControl: hold the robot model\'s clip `clip` at `time` as the whole pose (locomotion off) until cleared.\n' +
     '    void setCineClip(int clip, float time, bool loop) { cineClip_ = clip; cineTime_ = time; cineLoop_ = loop; }\n' +
     '    void clearCineClip() { cineClip_ = -1; }\n'],
]);
ed('src/game/Character.cpp', [
    ['    if (vehicleModel_ && !vehicleRig_.built && vehicleModel_->valid()) buildVehicleRig(*vehicleModel_);\n\n',
     '    if (vehicleModel_ && !vehicleRig_.built && vehicleModel_->valid()) buildVehicleRig(*vehicleModel_);\n' +
     '    // A campaign matinee drives this pawn (AnimControl on a CHARACTER group): its clip is the pose.\n' +
     '    if (cineClip_ >= 0 && form_ == Form::Robot && robotModel_ && cineClip_ < (int)robotModel_->clips.size() && trans_ == Transition::None) {\n' +
     '        animName_ = robotModel_->clips[(size_t)cineClip_].name;\n' +
     '        assets::samplePose(*robotModel_, cineClip_, cineTime_, cineLoop_, basePose_);\n' +
     '        finalizePose(*robotModel_, dt);\n' +
     '        return;\n' +
     '    }\n\n'],
]);
console.log('ok');
