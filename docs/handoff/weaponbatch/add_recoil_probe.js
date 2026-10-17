// Diagnostic (not for merge): WFC_RECOILPROBE=<chassis> - the local pawn fires one shot of its default weapon; for 45 steps the held
// weapon's axes are logged as yaw / pitch deltas (degrees, + = right / up) relative to the step before the shot, pawn frame. Run with and
// without WFC_NORECOIL to separate the recoil from the fire animation. Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) { if (s.split(a).length !== 2) throw new Error(f + ': anchor: ' + a.slice(0, 60)); s = s.replace(a, b); }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/core/Application.h', [['    void runEventTest();\n', '    void runEventTest();\n    void runRecoilProbe();\n']]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_EVENTTEST")) { runEventTest(); return false; }',
     '    if (std::getenv("WFC_RECOILPROBE")) { runRecoilProbe(); return false; }\n    if (std::getenv("WFC_EVENTTEST")) { runEventTest(); return false; }'],
    ['// WFC_PACINGTEST: presented camera',
     'void Application::runRecoilProbe() {\n' +
     '    const float dt = 1.0f / 60.0f;\n' +
     '    platform::InputFrame idle, fire;\n' +
     '    fire.pressed[(int)platform::Button::Fire] = true; fire.down[(int)platform::Button::Fire] = true;\n' +
     '    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);\n' +
     '    world_.launchMatch(L);\n' +
     '    game::CharacterSelection cs; cs.type = 1; cs.chassisId = std::getenv("WFC_RECOILPROBE");\n' +
     '    world_.match().selectCharacter(world_.localMatchPlayer(), cs);\n' +
     '    for (int i = 0; i < 12 * 60; ++i) { world_.handleInput(idle, dt); world_.tick(dt); }\n' +
     '    const game::Character& pc = world_.player().pawn();\n' +
     '    auto axes = [&](core::Vec3& x, core::Vec3& y, core::Vec3& z) { const core::Mat4& m = pc.weaponWorld(); x = core::normalize({m.m[0], m.m[1], m.m[2]}); y = core::normalize({m.m[4], m.m[5], m.m[6]}); z = core::normalize({m.m[8], m.m[9], m.m[10]}); };\n' +
     '    const float yaw0 = pc.yaw();\n' +
     '    const core::Vec3 fwd = core::forwardFromYawPitch(yaw0, 0.0f), up{0, 1, 0}, right = core::normalize(core::cross(fwd, up));\n' +
     '    core::Vec3 x0, y0, z0; axes(x0, y0, z0);\n' +
     '    auto ang = [&](const core::Vec3& v, const core::Vec3& v0) {   // yaw (+ right) / pitch (+ up) of v minus v0, pawn frame\n' +
     '        auto yp = [&](const core::Vec3& a, float& yw, float& pt) { yw = std::atan2(core::dot(a, right), core::dot(a, fwd)) * 57.29578f; pt = std::asin(core::clampf(core::dot(a, up), -1.0f, 1.0f)) * 57.29578f; };\n' +
     '        float ya, pa, yb, pb; yp(v, ya, pa); yp(v0, yb, pb); char buf[64]; std::snprintf(buf, sizeof buf, "%+6.1f/%+6.1f", ya - yb, pa - pb); return std::string(buf);\n' +
     '    };\n' +
     '    LOG_INFO("RECOILPROBE %s weapon %s yaw %.3f", cs.chassisId.c_str(), pc.weapon().def ? pc.weapon().def->id : "-", yaw0);\n' +
     '    for (int i = 0; i < 45; ++i) {\n' +
     '        world_.handleInput(i == 0 ? fire : idle, dt); world_.tick(dt);\n' +
     '        core::Vec3 x, y, z; axes(x, y, z);\n' +
     '        LOG_INFO("RECOILPROBE step %2d  X %s  Y %s  Z %s", i, ang(x, x0).c_str(), ang(y, y0).c_str(), ang(z, z0).c_str());\n' +
     '    }\n' +
     '}\n\n' +
     '// WFC_PACINGTEST: presented camera'],
]);
console.log('ok');
ed('src/core/Application_Frontend.cpp', [['"WFC_EVENTTEST", ', '"WFC_EVENTTEST", "WFC_RECOILPROBE", ']]);
console.log('ok (direct-boot list)');
