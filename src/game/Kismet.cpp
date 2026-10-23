// Campaign Kismet runtime - see Kismet.h. Stock op semantics follow UE3 (Engine.SeqAct_Delay / Gate / Switch / RandomSwitch /
// SeqCond_*, SeqAct_ActivateRemoteEvent) [H: the stock bodies are not decoded for this game, RE CAMPAIGN_KISMET_CLASSES_RE §2].
#include "game/Kismet.h"
#include "core/Log.h"

#include <algorithm>
#include <map>

namespace game {

static std::string parentOf(const std::string& path) {
    const size_t k = path.find_last_of('.');
    return k == std::string::npos ? std::string() : path.substr(0, k);
}
static bool endsWith(const std::string& s, const std::string& t) { return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; }

bool KismetVM::load(const assets::Json& root) {
    ops_.clear(); vars_.clear(); byPath_.clear(); varByPath_.clear(); stacks_.clear(); seqChildren_.clear(); seqRoots_.clear(); pending_.clear(); time_ = 0.0f; tickNo_ = 0;
    const assets::Json& levels = root["levels"];
    if (!levels.isObject()) return false;
    // Pass 1: variables and ops by path.
    for (const auto& lv : levels.obj)
        for (const assets::Json& o : lv.second.arr) {
            const std::string cls = o["class"].asString(), path = o["object"].asString();
            if (cls.rfind("SeqVar_", 0) == 0 || cls.rfind("TnSeqVar_", 0) == 0) {
                Var v; v.path = path; v.cls = cls;
                const assets::Json& a = o["authored"];
                v.i = a["IntValue"].asInt(0); v.f = a["FloatValue"].asFloat(0.0f); v.b = a["bValue"].asInt(0) != 0 || a["bValue"].asBool(false);
                v.s = a["StrValue"].asString(); v.obj = a["ObjValue"].asString(); v.findName = a["FindVarName"].asString(); v.varName = a["VarName"].asString(); v.buddyIndex = a["BuddyIndex"].asInt(1);
                varByPath_[path] = (int)vars_.size();
                vars_.push_back(v);
                continue;
            }
            Op op; op.path = path; op.cls = cls; op.seq = parentOf(path); op.authored = o["authored"];
            for (size_t k = 0; k < o["inputs"].size(); ++k) op.inputs.push_back(o["inputs"][k].asString());
            op.hasIncoming = o["incoming"].size() > 0;
            op.enabled = op.authored["bEnabled"].asBool(true);   // authored disabled events start disabled (RE K2)
            byPath_[path] = (int)ops_.size();
            ops_.push_back(op);
        }
    // Pass 2: links (outputs -> op inputs) and variable links; SeqVar_Named targets by VarName (all levels).
    for (const auto& lv : levels.obj)
        for (const assets::Json& o : lv.second.arr) {
            const int i = find(o["object"].asString());
            if (i < 0) continue;
            Op& op = ops_[(size_t)i];
            for (const auto& out : o["outputs"].obj) {
                std::vector<Link> ls;
                for (const assets::Json& l : out.second.arr) { const int t = find(l["op"].asString()); if (t >= 0) ls.push_back({t, l["input_index"].asInt(0)}); }
                op.outputs.push_back({out.first, ls});
            }
            for (const auto& vl : o["variables"].obj) {
                std::vector<int> vs;
                for (const assets::Json& v : vl.second.arr) { auto it = varByPath_.find(v["variable"].asString()); if (it != varByPath_.end()) vs.push_back(it->second); }
                op.vars.push_back({vl.first, vs});
            }
            op.gateOpen = op.authored["bOpen"].asBool(true);
        }
    {   // the sequence tree: a queue per owning sequence; children = the Sequence ops of a sequence, in load order
        std::map<std::string, int> sid;
        auto idOf = [&](const std::string& p) {
            auto it = sid.find(p);
            if (it != sid.end()) return it->second;
            const int n = (int)stacks_.size(); sid[p] = n; stacks_.emplace_back(); seqChildren_.emplace_back(); return n;
        };
        for (Op& op : ops_) op.seqId = idOf(op.seq);
        std::vector<int> parent(stacks_.size(), -1);
        for (const Op& op : ops_)
            if (op.cls == "Sequence") {
                const int c = idOf(op.path), p = op.seqId;
                if ((size_t)c >= parent.size()) parent.resize((size_t)c + 1, -1);
                if (c != p && parent[(size_t)c] < 0) { parent[(size_t)c] = p; seqChildren_[(size_t)p].push_back(c); }
            }
        parent.resize(stacks_.size(), -1);
        for (size_t k = 0; k < stacks_.size(); ++k) if (parent[k] < 0) seqRoots_.push_back((int)k);
    }
    for (Var& v : vars_)
        if (v.cls == "SeqVar_Named" && !v.findName.empty())
            for (size_t k = 0; k < vars_.size(); ++k) if (vars_[k].varName == v.findName && vars_[k].cls != "SeqVar_Named") { v.named = (int)k; break; }
    return !ops_.empty();
}

const KismetVM::Var* KismetVM::resolve(int i) const {
    if (i < 0 || (size_t)i >= vars_.size()) return nullptr;
    const Var& v = vars_[(size_t)i];
    return v.named >= 0 ? &vars_[(size_t)v.named] : &v;
}

std::vector<int> KismetVM::varLink(int op, const std::string& link) const {
    for (const auto& v : ops_[(size_t)op].vars) if (v.first == link) return v.second;
    return {};
}
int KismetVM::intOf(int op, const std::string& link, int def) const {
    for (int i : varLink(op, link)) if (const Var* v = resolve(i)) return v->i;
    return ops_[(size_t)op].authored[link].asInt(def);
}
float KismetVM::floatOf(int op, const std::string& link, float def) const {
    for (int i : varLink(op, link)) if (const Var* v = resolve(i)) return v->cls == "SeqVar_Int" ? (float)v->i : v->f;
    return ops_[(size_t)op].authored[link].asFloat(def);
}
std::string KismetVM::objectOf(int op, const std::string& link) const {
    for (int i : varLink(op, link)) if (const Var* v = resolve(i)) if (!v->obj.empty()) return v->obj;
    return {};
}

void KismetVM::impulse(int op, int input) { if (op >= 0) { pending_.push_back({op, input}); flush(); } }

// An output's targets in link order; they reach the stack reversed when the op's step ends (flush), so they run depth-first in order.
void KismetVM::output(int op, const std::string& name) {
    for (const auto& o : ops_[(size_t)op].outputs)
        if (o.first == name) for (const Link& l : o.second) pending_.push_back(l);
}

// SequenceEvent.CheckActivate: enabled, MaxTriggerCount (0 = unlimited), ReTriggerDelay. Defaults from the CDO chain where the export
// has none [C, RE KISMET_VM_CORE_RE section 1]: SequenceEvent 1 / 0 s; SequenceActivated, Console, TnSeqEvent_Interact unlimited; Touch 0.1 s.
bool KismetVM::fireEvent(int i, const std::string& outName) {
    Op& e = ops_[(size_t)i];
    if (!e.enabled) return false;
    const bool unlimitedClass = e.cls == "SeqEvent_Console" || e.cls == "SeqEvent_SequenceActivated" || e.cls == "TnSeqEvent_Interact";
    const int maxCount = e.authored["MaxTriggerCount"].isNumber() ? e.authored["MaxTriggerCount"].asInt(1) : (unlimitedClass ? 0 : 1);
    if (maxCount > 0 && e.triggerCount >= maxCount) return false;
    const float delay = e.authored["ReTriggerDelay"].asFloat(e.cls == "SeqEvent_Touch" ? 0.1f : 0.0f);
    if (e.triggerCount > 0 && time_ - e.lastTrigger < delay) return false;
    ++e.triggerCount; e.lastTrigger = time_;
    ++activations[e.cls];
    if (log) LOG_INFO("KISMET t %.2f event %s %s", time_, e.cls.c_str(), e.path.c_str());
    output(i, outName);
    flush();
    return true;
}

int KismetVM::fireEvents(const std::string& cls, const std::function<bool(const Op&)>& filter) {
    int n = 0;
    for (size_t i = 0; i < ops_.size(); ++i)
        if (ops_[i].cls == cls && (!filter || filter(ops_[i]))) n += fireEvent((int)i) ? 1 : 0;
    return n;
}

int KismetVM::remoteEvent(const std::string& name) {
    return fireEvents("SeqEvent_RemoteEvent", [&](const Op& o) { return o.authored["EventName"].asString() == name; });
}
int KismetVM::consoleEvent(const std::string& name) {
    return fireEvents("SeqEvent_Console", [&](const Op& o) { return o.authored["ConsoleEventName"].asString() == name; });
}

void KismetVM::begin(const std::string& checkpoint, bool restart) {
    activeCheckpoint = checkpoint;
    auto bound = [&](const Op& o) {
        const std::string org = o.authored["Originator"].asString();
        bool fire;
        if (org.find("Checkpoint") != std::string::npos) fire = !checkpoint.empty() && endsWith(org, checkpoint);
        else fire = checkpoint.empty() || o.authored["bFireForAllCheckpoints"].asBool(false);
        if (fire) ops_[(size_t)find(o.path)].triggerCount = 0;   // TriggerCount = 0, then CheckActivate
        return fire;
    };
    if (!restart) { fireEvents("SeqEvent_PreGameplaySetup", bound); run(); }
    fireEvents("SeqEvent_SequenceActivated", [](const Op& o) { return endsWith(o.seq, ".PersistentLevel.Main_Sequence") || o.seq == "Main_Sequence"; });
    run();
    fireEvents("SeqEvent_GameplayStarted", bound);   // queued: runs with the first normal tick [C]
}

// Stock UE3 ops [H].
bool KismetVM::builtin(int i, int input) {
    Op& o = ops_[(size_t)i];
    const std::string& c = o.cls;
    if (c == "SeqAct_Delay") {   // Start / Stop / Pause; Finished at 0, Aborted on Stop
        if (input == 0) {   // UpdateOp runs in the activation step [C]: the first dt comes off now
            o.timer = floatOf(i, "Duration", o.authored["Duration"].asFloat(1.0f)) - dt_; o.latent = true; o.paused = false; o.startTick = tickNo_;
            if (o.timer <= 0.0f) { o.latent = false; output(i, "Finished"); }
        }
        else if (input == 1) { if (o.latent) { o.latent = false; output(i, "Aborted"); } }
        else if (input == 2) o.paused = !o.paused;
        return true;
    }
    if (c == "SeqAct_Gate") {   // In / Open / Close / Toggle; AutoCloseCount closes after that many passes
        if (input == 0 && o.gateOpen) {
            output(i, "Out");
            const int ac = o.authored["AutoCloseCount"].asInt(0);
            if (ac > 0 && ++o.gateCount >= ac) { o.gateOpen = false; o.gateCount = 0; }
        } else if (input == 1) o.gateOpen = true;
        else if (input == 2) o.gateOpen = false;
        else if (input == 3) o.gateOpen = !o.gateOpen;
        return true;
    }
    if (c == "SeqAct_ActivateRemoteEvent") { remoteEvent(o.authored["EventName"].asString()); output(i, "Out"); return true; }
    if (c == "SeqAct_RandomSwitch" || c == "SeqAct_Switch") {
        const int n = std::max(1, o.authored["LinkCount"].asInt((int)o.outputs.size()));
        if (o.linkDisabled.size() != (size_t)n) o.linkDisabled.assign((size_t)n, 0);
        const bool autoDisable = o.authored["bAutoDisableLinks"].asBool(false);
        const bool looping = o.authored["bLooping"].asBool(false);
        int pick = -1;
        if (c == "SeqAct_RandomSwitch") {
            std::vector<int> open;
            for (int k = 0; k < n; ++k) if (!o.linkDisabled[(size_t)k]) open.push_back(k);
            if (open.empty() && looping) { o.linkDisabled.assign((size_t)n, 0); for (int k = 0; k < n; ++k) open.push_back(k); }
            if (!open.empty()) pick = open[(size_t)randInt((int)open.size())];
        } else {   // SeqAct_Switch: the "Index" variable (1-based; default 1), IncrementAmount after firing, bLooping wraps
            std::vector<int> idx = varLink(i, "Index");
            int at = idx.empty() ? o.timer > 0.0f ? (int)o.timer : 1 : (resolve(idx[0]) ? resolve(idx[0])->i : 1);
            if (at >= 1 && at <= n && !o.linkDisabled[(size_t)at - 1]) pick = at - 1;
            const int inc = o.authored["IncrementAmount"].asInt(o.authored["bIncrement"].asBool(false) ? 1 : 0);
            if (inc) {
                at += inc;
                if (at > n) at = looping ? 1 : n + 1;
                if (!idx.empty()) { if (Var* v = var(idx[0])) v->i = at; } else o.timer = (float)at;
            }
        }
        if (pick >= 0) {
            output(i, "Link " + std::to_string(pick + 1));
            if (autoDisable || c == "SeqAct_RandomSwitch") o.linkDisabled[(size_t)pick] = autoDisable || looping ? 1 : 0;
        }
        return true;
    }
    if (c == "SeqCond_Increment" || c == "SeqCond_CompareInt" || c == "SeqCond_CompareFloat") {
        float a, b;
        if (c == "SeqCond_Increment") {
            std::vector<int> av = varLink(i, "A");
            const int amt = o.authored["IncrementAmount"].asInt(1);
            if (!av.empty()) if (Var* v = var(vars_[(size_t)av[0]].named >= 0 ? vars_[(size_t)av[0]].named : av[0])) v->i += amt;
            a = (float)intOf(i, "A", o.authored["ValueA"].asInt(0)); b = (float)intOf(i, "B", o.authored["ValueB"].asInt(0));
        } else if (c == "SeqCond_CompareInt") { a = (float)intOf(i, "A", o.authored["ValueA"].asInt(0)); b = (float)intOf(i, "B", o.authored["ValueB"].asInt(0)); }
        else { a = floatOf(i, "A", o.authored["ValueA"].asFloat(0)); b = floatOf(i, "B", o.authored["ValueB"].asFloat(0)); }
        if (a <= b) output(i, "A <= B");
        if (a > b) output(i, "A > B");
        if (a == b) output(i, "A == B");
        if (a < b) output(i, "A < B");
        if (a >= b) output(i, "A >= B");
        return true;
    }
    if (c == "SeqCond_CompareBool") {
        bool v = false; for (int k : varLink(i, "Bool")) if (const Var* x = resolve(k)) v = x->b;
        output(i, v ? "True" : "False");
        return true;
    }
    if (c == "SeqAct_SetInt" || c == "SeqAct_SetBool" || c == "SeqAct_SetFloat" || c == "SeqAct_SetString" || c == "SeqAct_SetObject") {
        for (int k : varLink(i, "Target")) {
            Var* t = var(vars_[(size_t)k].named >= 0 ? vars_[(size_t)k].named : k);
            if (!t) continue;
            if (c == "SeqAct_SetInt") t->i = intOf(i, "Value", o.authored["DefaultValue"].asInt(0));
            else if (c == "SeqAct_SetFloat") t->f = floatOf(i, "Value", o.authored["DefaultValue"].asFloat(0));
            else if (c == "SeqAct_SetBool") { bool v = o.authored["DefaultValue"].asBool(false); for (int q : varLink(i, "Value")) if (const Var* x = resolve(q)) v = x->b; t->b = v; }
            else if (c == "SeqAct_SetString") { std::string v = o.authored["DefaultValue"].asString(); for (int q : varLink(i, "Value")) if (const Var* x = resolve(q)) v = x->s; t->s = v; }
            else t->obj = objectOf(i, "Value");
        }
        output(i, "Out");
        return true;
    }
    if (c == "SeqAct_FinishSequence") {   // the enclosing Sequence's output (RE asked how its outputs link)
        const int seq = find(o.seq);
        if (seq >= 0) output(seq, o.authored["OutputLabel"].asString());
        return true;
    }
    if (c == "Sequence") {   // input k -> InputLinks[k].LinkedOp [C] when exported (sequence_inputs), else the k-th SequenceActivated [H]
        const assets::Json& si = o.authored["sequence_inputs"];
        for (size_t q = 0; q < si.size(); ++q)
            if (si[q]["index"].asInt(-1) == input) { const int ev = find(si[q]["linked_op"].asString()); if (ev >= 0) fireEvent(ev); return true; }
        int k = 0;
        for (size_t q = 0; q < ops_.size(); ++q)
            if (ops_[q].cls == "SeqEvent_SequenceActivated" && ops_[q].seq == o.path) { if (k++ == input) { fireEvent((int)q); break; } }
        return true;
    }
    if (c == "SeqAct_MutualExclusion") {   // RE WFC §69 (C): Enter takes the lock (or waits, FIFO); Exit releases -> Exited, the next waiter enters
        const int id = intOf(i, "OwnerID", 0);
        if (input == 0) {
            if (o.owner < 0) { o.owner = id; output(i, "Entered"); }
            else if (o.owner != id && std::find(o.waiting.begin(), o.waiting.end(), id) == o.waiting.end()) o.waiting.push_back(id);
        } else if (input == 1) {
            if (o.owner == id) {
                o.owner = -1; output(i, "Exited");
                if (!o.waiting.empty()) { o.owner = o.waiting.front(); o.waiting.erase(o.waiting.begin()); output(i, "Entered"); }
            } else o.waiting.erase(std::remove(o.waiting.begin(), o.waiting.end(), id), o.waiting.end());
        }
        return true;
    }
    if (c == "SeqAct_Log" || c == "SeqAct_DrawText") { output(i, "Out"); return true; }
    return false;
}

// USequence::UpdateOp [C]: the root sequences, each draining its own queue and then its nested sequences in array order. Work queued
// into a sequence whose pass already ran this call (a parent, an earlier sibling) waits for the next tick.
void KismetVM::run() {
    flush();
    for (int r : seqRoots_) drain(r);
}

// ExecuteActiveOps [C]: pop the last queued op, run it, push its targets reversed; at most 1000 steps per queue per call, the rest waits
// for the next tick (not dropped).
void KismetVM::drain(int seq) {
    int steps = 0;
    while (!stacks_[(size_t)seq].empty()) {
        if (++steps > 1000) { ++queueCapHits; if (log) LOG_INFO("KISMET: 1000-step cap at t %.2f (%zu carried over)", time_, stacks_[(size_t)seq].size()); break; }
        const Link l = stacks_[(size_t)seq].back(); stacks_[(size_t)seq].pop_back();
        Op& o = ops_[(size_t)l.op];
        ++activations[o.cls];
        if (log) LOG_INFO("KISMET t %.2f %s [%d] %s", time_, o.cls.c_str(), l.input, o.path.c_str());
        if (builtin(l.op, l.input)) { flush(); continue; }
        if (o.cls.find("SeqEvent") != std::string::npos) { output(l.op, "Out"); flush(); continue; }   // an event's input (Toggle on events: host)
        if (host_ && host_->activate(*this, l.op, l.input)) {   // a host latent op takes its first update in the activation step
            if (o.latent && o.startTick != tickNo_) { o.startTick = tickNo_; host_->updateLatent(*this, l.op, dt_); }
            flush(); continue;
        }
        ++defaulted[o.cls];
        if (!o.outputs.empty()) output(l.op, o.outputs.front().first);   // the generic In -> Out
        flush();
    }
    for (size_t k = 0; k < seqChildren_[(size_t)seq].size(); ++k) drain(seqChildren_[(size_t)seq][k]);
}

void KismetVM::tick(float dt) {
    time_ += dt; dt_ = dt; ++tickNo_;
    run();   // this tick's new work (with the carried-over steps); new latent ops take their first update in it
    for (size_t i = 0; i < ops_.size(); ++i) {   // then every older latent op once [C: re-queued after the drain]
        Op& o = ops_[i];
        if (!o.latent || o.startTick == tickNo_) continue;
        if (o.cls == "SeqAct_Delay") {
            if (!o.paused && (o.timer -= dt) <= 0.0f) { o.latent = false; output((int)i, "Finished"); }
        } else if (host_) host_->updateLatent(*this, (int)i, dt);
        flush();
    }
    run();
}

} // namespace game
