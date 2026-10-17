// NAVQ yardstick for the hover item: hover-vehicle tipping (WFC_NAVQTEST only; a separate line so the SUMMARY format other tools parse
// stays unchanged). Per bot: time in hover vehicle form (car / truck / tank hovering, not Driving, not jets), time tipped (world |pitch|
// or |roll| > 30 deg, the IsUnstable gate), tip episodes, the longest episode.
//   NAVQ TIP map <map> ai <ai>: hover bot-min <m>, tipped <p>% of hover time, episodes <n> (<r> / hover bot-min), longest <s> s
// Apply to the base and the patch trees alike (the before / after compare). Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/core/Application.cpp', [
    ['    struct Acc { double travelT = 0, slowT = 0, offT = 0, aliveT = 0, dist = 0;',
     '    struct Acc { double hoverT = 0, tipT = 0, tipRun = 0, tipMax = 0; int tipEp = 0; bool tipped = false;   // NAVQ TIP\n' +
     '                 double travelT = 0, slowT = 0, offT = 0, aliveT = 0, dist = 0;'],
    ['            a.aliveT += dt;\n',
     '            a.aliveT += dt;\n' +
     '            {   // NAVQ TIP: a hover vehicle (not Driving, not a jet) past the 30 deg IsUnstable gate\n' +
     '                const bool hover = pc.moveForm() == game::Form::Vehicle && !pc.vehicleState().driving && pc.vehicleParams().form != game::VehicleFormType::Jet;\n' +
     '                const float lim = 30.0f * 0.0174533f;\n' +
     '                const bool tip = hover && (std::fabs(pc.vehicleState().pitch) > lim || std::fabs(pc.vehicleState().roll) > lim);\n' +
     '                if (hover) a.hoverT += dt;\n' +
     '                if (tip) { a.tipT += dt; a.tipRun += dt; a.tipMax = std::max(a.tipMax, a.tipRun); if (!a.tipped) ++a.tipEp; }\n' +
     '                else a.tipRun = 0.0;\n' +
     '                a.tipped = tip;\n' +
     '            }\n'],
    ['    LOG_INFO("NAVQ SUMMARY map %s ai %s bots %d,',
     '    {\n' +
     '        double hT = 0, tT = 0, tMax = 0; int ep = 0;\n' +
     '        for (const auto& kv : acc) { hT += kv.second.hoverT; tT += kv.second.tipT; ep += kv.second.tipEp; tMax = std::max(tMax, kv.second.tipMax); }\n' +
     '        LOG_INFO("NAVQ TIP map %s ai %s: hover bot-min %.2f, tipped %.2f%% of hover time, episodes %d (%.2f / hover bot-min), longest %.2f s",\n' +
     '                 world_.mapName().c_str(), smart == nb ? "Smart" : smart == 0 ? "Classic" : "mixed", hT / 60.0, 100.0 * tT / std::max(1e-6, hT), ep,\n' +
     '                 ep / std::max(1e-6, hT / 60.0), tMax);\n' +
     '    }\n' +
     '    LOG_INFO("NAVQ SUMMARY map %s ai %s bots %d,'],
]);
console.log('ok');
