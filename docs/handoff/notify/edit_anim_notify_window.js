// Systems' anim-notify hook (package 7 authored FX / sound wiring): per pawn and animation update, every sampled body layer's playing time
// window [t0, t1) goes to World::onAnimNotifyWindow(matchPlayer, animSet, sequence, t0, t1, backwards, weight); Systems fires the authored
// notify rows (tools/systems/gen_anim_notifies.py) that fall inside it. Gameplay side only: the bookkeeping and the call; the body of
// onAnimNotifyWindow is an empty stub here that Systems' patch fills.
// - Layers traced: the locomotion blend (weight = blend weight), the base clip (playClip: idle / jump / fall / vehicle / transform; 1),
//   reload (reloadW_), the melee / action layer (actionW_), the vehicle hover additive (hoverW_), the transform hand-off clip (1).
//   Not traced: rig-building samples (t 0) and the separate weapon-arm model (WeaponMesh notifies are Systems').
// - Windows: a clip that kept playing gives [previous t, t); a loop wrap gives [previous t, duration) + [0, t); a one-shot that restarted
//   (t went down) gives [0, t); a clip that just started gives [max(0, t - dt), t). Empty windows are not sent. Nothing is played backwards in ours: backwards is
//   always false. Distant bots update their animation every few steps (simulateAnimationLod): one window covers the accumulated time.
// - animSet / sequence = the clip's authored source (character.json robot / vehicle animations: source_animset, sequence), looked up
//   by clip name in the pawn's chassis; clips without one are not sent.
// Presentation only (no sim state read back): SIMHASH identical. Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/ChassisDef.h', [
    ['#include <string>\n', '#include <map>\n#include <string>\n'],
    ['    std::vector<AnimRef> robotAnims;\n',
     '    std::vector<AnimRef> robotAnims;\n' +
     '    // Clip name -> its authored AnimSet object path and SequenceName (character.json robot / vehicle animations: source_animset,\n' +
     '    // sequence), as the authored notify rows key them.\n' +
     '    struct ClipSource { std::string animSet, sequence; };\n' +
     '    std::map<std::string, ClipSource> clipSources;\n'],
]);
ed('src/game/ChassisDef.cpp', [[
    '            if (!r.name.empty() && !r.source.empty()) d.robotAnims.push_back(r);\n' +
    '        }\n' +
    '    }\n',
    '            if (!r.name.empty() && !r.source.empty()) d.robotAnims.push_back(r);\n' +
    '        }\n' +
    '    }\n' +
    '    for (const char* form : {"robot", "vehicle"}) {\n' +
    '        const assets::Json& an = c[form]["animations"];\n' +
    '        for (size_t i = 0; i < an.size(); ++i) {\n' +
    '            const std::string name = an[i]["name"].asString(), set = an[i]["source_animset"].asString();\n' +
    '            if (name.empty() || set.empty()) continue;\n' +
    '            const std::string seq = an[i]["sequence"].asString();\n' +
    '            d.clipSources.emplace(name, ChassisDef::ClipSource{set, seq.empty() ? name : seq});\n' +
    '        }\n' +
    '    }\n'
]]);
ed('src/game/World.cpp', [
    ['    { sysprof::Scope sp(sysprof::Anim); player_.pawn().updateAnimation(dt); }\n',
     '    { sysprof::Scope sp(sysprof::Anim); player_.pawn().updateAnimation(dt); }\n' +
     '    drainAnimNotifies();   // [Systems] authored body notifies: every participant\'s anim windows of this step\n'],
    ['void World::startLocalMelee(bool whirlwind) {\n',
     '// Hands every participant\'s anim windows of this step to Systems\' notify dispatcher (presentation only).\n' +
     'void World::drainAnimNotifies() {\n' +
     '    auto drain = [this](Character& c, int mp) {\n' +
     '        const std::map<std::string, ChassisDef::ClipSource>& src = c.chassis().clipSources;\n' +
     '        for (const Character::AnimWindow& w : c.animWindows()) {\n' +
     '            auto it = src.find(w.clip->name);\n' +
     '            if (it == src.end()) continue;   // no authored source (not a chassis clip): no notifies to fire\n' +
     '            onAnimNotifyWindow(mp, it->second.animSet.c_str(), it->second.sequence.c_str(), w.t0, w.t1, false, w.weight);\n' +
     '        }\n' +
     '        c.clearAnimWindows();\n' +
     '    };\n' +
     '    drain(player_.pawn(), localPlayer_);\n' +
     '    for (MatchOpponent* o : opponents_) drain(o->pawn(), o->matchPlayer());\n' +
     '}\n\n' +
     '// [Systems] fills: fire the authored notify rows of (animSet, sequence) with time in [t0, t1). Gameplay\'s stub.\n' +
     'void World::onAnimNotifyWindow(int matchPlayer, const char* animSet, const char* sequence, float t0, float t1, bool backwards, float weight) {\n' +
     '    (void)matchPlayer; (void)animSet; (void)sequence; (void)t0; (void)t1; (void)backwards; (void)weight;\n' +
     '}\n\n' +
     'void World::startLocalMelee(bool whirlwind) {\n'],
]);
ed('src/game/World.h', [[
    '    void startLocalMelee(bool whirlwind);\n',
    '    void startLocalMelee(bool whirlwind);\n' +
    '    void drainAnimNotifies();\n' +
    '    // One body animation layer\'s playing-time window [t0, t1) this step (Systems: the authored AnimNotifies in it).\n' +
    '    void onAnimNotifyWindow(int matchPlayer, const char* animSet, const char* sequence, float t0, float t1, bool backwards, float weight);\n'
]]);
ed('src/game/Character.h', [
    ['    void updateAnimation(float dt);\n',
     '    void updateAnimation(float dt);\n' +
     '    // Anim notify windows (presentation): each traced layer\'s [t0, t1) since the previous update; World drains them per step.\n' +
     '    struct AnimWindow { const assets::AnimClip* clip; float t0, t1, weight; };\n' +
     '    const std::vector<AnimWindow>& animWindows() const { return animWindows_; }\n' +
     '    void clearAnimWindows() { animWindows_.clear(); }\n'],
    ['    std::string animName_ = "-";\n',
     '    std::string animName_ = "-";\n' +
     '    struct AnimTrace { const assets::AnimClip* clip; float t, weight; bool loop; };\n' +
     '    std::vector<AnimTrace> animTrace_, animTracePrev_;   // this update\'s / the previous update\'s sampled layers\n' +
     '    std::vector<AnimWindow> animWindows_;\n' +
     '    float animTraceDt_ = 0.0f;\n' +
     '    void traceClip(const assets::SkinnedModel& mdl, int clip, float t, bool loop, float weight) {\n' +
     '        if (clip < 0 || (size_t)clip >= mdl.clips.size() || weight <= 0.0f) return;\n' +
     '        const assets::AnimClip* c = &mdl.clips[(size_t)clip];\n' +
     '        for (AnimTrace& a : animTrace_) if (a.clip == c) { if (weight > a.weight) a.weight = weight; return; }\n' +
     '        animTrace_.push_back({c, t, weight, loop});\n' +
     '    }\n' +
     '    void endAnimTrace();\n'],
]);
ed('src/game/Character.cpp', [
    ['void Character::updateAnimation(float dt) {\n' +
     '    const assets::SkinnedModel* mdl = currentModel();\n' +
     '    if (!mdl || !mdl->valid()) return;\n',
     '// The traced layers -> notify windows against the previous update (see Character.h AnimWindow).\n' +
     'void Character::endAnimTrace() {\n' +
     '    if (animWindows_.size() > 256) animWindows_.clear();   // never drained (no World loop): bounded\n' +
     '    for (const AnimTrace& a : animTrace_) {\n' +
     '        const float dur = a.clip->duration;\n' +
     '        float t1 = a.t;\n' +
     '        if (a.loop && dur > 0.0f) { t1 = std::fmod(t1, dur); if (t1 < 0.0f) t1 += dur; }\n' +
     '        const AnimTrace* p = nullptr;\n' +
     '        for (const AnimTrace& q : animTracePrev_) if (q.clip == a.clip) { p = &q; break; }\n' +
     '        if (!p) { const float t0 = std::max(0.0f, t1 - animTraceDt_); if (t1 > t0) animWindows_.push_back({a.clip, t0, t1, a.weight}); continue; }\n' +
     '        float t0 = p->t;\n' +
     '        if (a.loop && dur > 0.0f) { t0 = std::fmod(t0, dur); if (t0 < 0.0f) t0 += dur; }\n' +
     '        if (t1 > t0) animWindows_.push_back({a.clip, t0, t1, a.weight});\n' +
     '        else if (t1 < t0) {\n' +
     '            if (a.loop && dur > t0) animWindows_.push_back({a.clip, t0, dur, a.weight});\n' +
     '            if (t1 > 0.0f) animWindows_.push_back({a.clip, 0.0f, t1, a.weight});   // the wrap\'s second part / a restarted one-shot\n' +
     '        }\n' +
     '    }\n' +
     '    animTracePrev_.swap(animTrace_);\n' +
     '    animTrace_.clear();\n' +
     '}\n\n' +
     'void Character::updateAnimation(float dt) {\n' +
     '    const assets::SkinnedModel* mdl = currentModel();\n' +
     '    if (!mdl || !mdl->valid()) return;\n' +
     '    animTrace_.clear(); animTraceDt_ = dt;\n' +
     '    struct TraceEnd { Character* c; ~TraceEnd() { c->endAnimTrace(); } } traceEnd{this};   // every return path\n'],
    ['        float t = locoPhase_ * mdl.clips[(size_t)clips[i]].duration;\n',
     '        float t = locoPhase_ * mdl.clips[(size_t)clips[i]].duration;\n' +
     '        traceClip(mdl, clips[i], t, true, w[i]);\n'],
    ['    assets::samplePose(mdl, clip, animTime_, loop, basePose_);\n    finishBaseBlend(dt);\n',
     '    assets::samplePose(mdl, clip, animTime_, loop, basePose_);\n    traceClip(mdl, clip, animTime_, loop, 1.0f);\n    finishBaseBlend(dt);\n'],
    ['        assets::samplePose(mdl, R.reloadClip, reloadT_, false, layerPose_);\n',
     '        assets::samplePose(mdl, R.reloadClip, reloadT_, false, layerPose_);\n' +
     '        traceClip(mdl, R.reloadClip, reloadT_, false, reloadW_);\n'],
    ['            assets::samplePose(mdl, actionClip_, std::min(actionT_, len), false, layerPose_);\n',
     '            assets::samplePose(mdl, actionClip_, std::min(actionT_, len), false, layerPose_);\n' +
     '            traceClip(mdl, actionClip_, std::min(actionT_, len), false, actionW_);\n'],
    ['        assets::samplePose(mdl, vehicleRig_.hoverAddClip, hoverT_, true, layerPose_, /*additive*/ true);\n',
     '        assets::samplePose(mdl, vehicleRig_.hoverAddClip, hoverT_, true, layerPose_, /*additive*/ true);\n' +
     '        traceClip(mdl, vehicleRig_.hoverAddClip, hoverT_, true, hoverW_);\n'],
    ['                assets::samplePose(*mdl, ic, animTime_, false, basePose_);\n',
     '                assets::samplePose(*mdl, ic, animTime_, false, basePose_);\n' +
     '                traceClip(*mdl, ic, animTime_, false, 1.0f);\n'],
]);
console.log('ok');
