// 300 fps sim plan fix F: KismetVM::tick updated the latent ops by scanning every op of the level each tick. An op becomes latent only
// in its own activation (builtin Delay, the host's activate), which runs inside drain(); so every op drain() pops is noted, and the
// latent pass walks the latent list (the noted ops that are latent, merged in index order) instead of all ops. Same ops, same order,
// same once-per-tick rule (RE KISMET_VM_SCHEDULING_RE: latent ops once per tick including the activation tick, parent then nested in
// array order); ops that left latent leave the list after the pass. Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/Kismet.h', [
    ['    int tickNo_ = 0; float dt_ = 0.0f;' + NL,
     '    int tickNo_ = 0; float dt_ = 0.0f;' + NL +
     '    // The latent pass (tick): every op drain() popped since the last pass (latent is only set in an activation), and the latent ops' + NL +
     '    // in index order with a membership flag.' + NL +
     '    std::vector<int> touched_, latentList_;' + NL +
     '    std::vector<uint8_t> inLatent_;' + NL],
]);
ed('src/game/Kismet.cpp', [
    ['    ops_.clear(); vars_.clear(); byPath_.clear(); varByPath_.clear(); stacks_.clear(); seqChildren_.clear(); seqRoots_.clear(); pending_.clear(); time_ = 0.0f; tickNo_ = 0;',
     '    ops_.clear(); vars_.clear(); byPath_.clear(); varByPath_.clear(); stacks_.clear(); seqChildren_.clear(); seqRoots_.clear(); pending_.clear(); time_ = 0.0f; tickNo_ = 0;' + NL +
     '    touched_.clear(); latentList_.clear(); inLatent_.clear();'],
    ['        const Link l = stacks_[(size_t)seq].back(); stacks_[(size_t)seq].pop_back();' + NL,
     '        const Link l = stacks_[(size_t)seq].back(); stacks_[(size_t)seq].pop_back();' + NL +
     '        touched_.push_back(l.op);   // (its activation may make it latent: the latent pass picks it up)' + NL],
    ['    for (size_t i = 0; i < ops_.size(); ++i) {   // then every older latent op once [C: re-queued after the drain]' + NL +
     '        Op& o = ops_[i];' + NL,
     '    if (inLatent_.size() != ops_.size()) {   // first pass after a load: every op already latent' + NL +
     '        inLatent_.assign(ops_.size(), 0); latentList_.clear(); touched_.clear();' + NL +
     '        for (size_t i = 0; i < ops_.size(); ++i) if (ops_[i].latent) { inLatent_[i] = 1; latentList_.push_back((int)i); }' + NL +
     '    }' + NL +
     '    bool added = false;' + NL +
     '    for (int t : touched_) if (ops_[(size_t)t].latent && !inLatent_[(size_t)t]) { inLatent_[(size_t)t] = 1; latentList_.push_back(t); added = true; }' + NL +
     '    touched_.clear();' + NL +
     '    if (added) std::sort(latentList_.begin(), latentList_.end());' + NL +
     '    for (size_t k = 0; k < latentList_.size(); ++k) {   // then every older latent op once, in index order [C: re-queued after the drain]' + NL +
     '        const size_t i = (size_t)latentList_[k];' + NL +
     '        Op& o = ops_[i];' + NL],
    ['        } else if (host_) host_->updateLatent(*this, (int)i, dt);' + NL + '        flush();' + NL + '    }' + NL,
     '        } else if (host_) host_->updateLatent(*this, (int)i, dt);' + NL + '        flush();' + NL + '    }' + NL +
     '    {   // the ops that left latent leave the list (re-noted at their next activation)' + NL +
     '        size_t w = 0;' + NL +
     '        for (int i : latentList_) { if (ops_[(size_t)i].latent) latentList_[w++] = i; else inLatent_[(size_t)i] = 0; }' + NL +
     '        latentList_.resize(w);' + NL +
     '    }' + NL],
]);
console.log('ok');
