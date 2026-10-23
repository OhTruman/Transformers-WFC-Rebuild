#pragma once
// Campaign Kismet runtime (the sequence VM over AssetTools' next_map/<chapter>_kismet.json: authored graph CONFIRMED; the VM core per
// RE KISMET_VM_CORE_RE / REVIEW_KISMET_VM_RE [C]: one ActiveSequenceOps queue per Sequence, drained parent first then the nested
// sequences in array order (USequence::UpdateOp); LIFO with a finished op's targets in link order depth-first, 1000 steps per queue per
// tick with the leftovers carried over, latent ops updated in their activation step and then once per tick, the CDO trigger defaults). Ops, links and variables load from the flat
// per-level object lists; sequence nesting is the dotted path. Stock ops (Delay, Gate, RemoteEvent, Switch, RandomSwitch, the
// compare / increment conditions, Set*) run here; every other op class goes to the Host (gameplay: checkpoints, pawn factories, AI
// moves, conversations, matinee, ...). An op neither handles fires its first output (the generic In -> Out default, RE WFC "Batch 2").
#include "assets/Json.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace game {

class KismetVM {
public:
    struct Link { int op = -1; int input = 0; };
    struct Var {
        std::string path, cls;
        int i = 0; float f = 0.0f; bool b = false; std::string s, obj, findName;   // SeqVar_Int / Float / Bool / String / Object / Named
        int buddyIndex = 1;   // TnSeqVar_Buddy BuddyIndex (1-based, CDO default 1)
        int named = -1;                                                         // SeqVar_Named: the var it resolved to (by VarName)
        std::string varName;                                                    // the var's own VarName (Named targets)
    };
    struct Op {
        std::string path, cls, seq;            // seq: the enclosing sequence's path
        assets::Json authored;
        std::vector<std::string> inputs;
        std::vector<std::pair<std::string, std::vector<Link>>> outputs;
        std::vector<std::pair<std::string, std::vector<int>>> vars;   // variable link name -> var indices
        bool hasIncoming = false;
        int startTick = -1;                   // the tick a latent op was activated in (its first update ran then)
        int seqId = -1;                       // the owning sequence's queue
        // runtime
        int triggerCount = 0; float lastTrigger = -1.0e9f; bool enabled = true;
        bool latent = false, paused = false; float timer = 0.0f;
        bool gateOpen = true; int gateCount = 0;
        std::vector<uint8_t> linkDisabled;    // Switch / RandomSwitch auto-disabled links
        int owner = -1;                       // MutualExclusion: the lock holder (OwnerID) and the FIFO wait list
        std::vector<int> waiting;
    };
    struct Host {
        virtual ~Host() = default;
        // A non-stock op's input fired: return true when the host took it (it fires outputs with vm.output, now or later; a latent op
        // stays latent until it does); false -> the generic default (the first output).
        virtual bool activate(KismetVM& vm, int op, int input) = 0;
        virtual void updateLatent(KismetVM& vm, int op, float dt) { (void)vm; (void)op; (void)dt; }
    };

    bool load(const assets::Json& root);       // every level of the chapter
    void setHost(Host* h) { host_ = h; }
    size_t opCount() const { return ops_.size(); }
    Op& op(int i) { return ops_[(size_t)i]; }
    const Op& op(int i) const { return ops_[(size_t)i]; }
    int find(const std::string& path) const { auto it = byPath_.find(path); return it == byPath_.end() ? -1 : it->second; }

    // Firing. impulse: queue an op's input; output: an op's named output (its linked inputs queued); events honour bEnabled,
    // MaxTriggerCount and ReTriggerDelay.
    void impulse(int op, int input);
    void output(int op, const std::string& name);
    bool fireEvent(int op, const std::string& outName = "Out");
    int fireEvents(const std::string& cls, const std::function<bool(const Op&)>& filter = {});
    int remoteEvent(const std::string& eventName);    // every SeqEvent_RemoteEvent with that EventName
    int consoleEvent(const std::string& name);        // SeqEvent_Console ConsoleEventName
    // Level start: PreGameplaySetup (skipped on an in-place checkpoint restart [C]), the SequenceActivated events directly in a level's
    // Main_Sequence [HIGH], then GameplayStarted - the Originator-bound ones only for the active checkpoint (its path suffix), the
    // unbound ones always [HIGH].
    // PerformPreGameplaySetup(CP) [C, RE KISMET_VM_CORE section 6]: CP = none on a map load, the restored checkpoint on a checkpoint start.
    // Startup events (PreGameplaySetup and GameplayStarted alike): Originator none -> CP none or bFireForAllCheckpoints; Checkpoint-bound
    // -> only that CP's (chosen once from the argument). Each fire resets TriggerCount first. The pre-gameplay pass runs only the
    // PreGameplaySetup chains; GameplayStarted waits for the first normal tick.
    void begin(const std::string& checkpoint, bool restart = false);
    std::string activeCheckpoint;   // the host keeps it current (SeqAct_SetActiveCheckpoint); begin() seeds it (diagnostics)
    void tick(float dt);
    float time() const { return time_; }
    void finishLatent(int op, const std::string& outName) { ops_[(size_t)op].latent = false; output(op, outName); }

    // Variables.
    std::vector<int> varLink(int op, const std::string& link) const;
    int varCount() const { return (int)vars_.size(); }
    Var* var(int i) { return i >= 0 && (size_t)i < vars_.size() ? &vars_[(size_t)i] : nullptr; }
    int intOf(int op, const std::string& link, int def) const;
    float floatOf(int op, const std::string& link, float def) const;
    std::string objectOf(int op, const std::string& link) const;   // the first object value (SeqVar_Object ObjValue / Named target)

    // Diagnostics.
    std::map<std::string, int> activations;   // per op class
    std::map<std::string, int> defaulted;     // classes nobody handled (generic first output)
    int queueCapHits = 0;
    bool log = false;

private:
    std::vector<Op> ops_;
    std::vector<Var> vars_;
    std::map<std::string, int> byPath_, varByPath_;
    std::vector<std::vector<Link>> stacks_;   // per sequence: ActiveSequenceOps, LIFO (the last queued runs first)
    std::vector<std::vector<int>> seqChildren_;   // nested sequences in array (load) order
    std::vector<int> seqRoots_;
    std::vector<Link> pending_;   // the current op's impulsed targets in link order (pushed reversed when it finishes)
    int tickNo_ = 0; float dt_ = 0.0f;
    void flush() { for (size_t k = pending_.size(); k-- > 0;) stacks_[(size_t)ops_[(size_t)pending_[k].op].seqId].push_back(pending_[k]); pending_.clear(); }
    void drain(int seq);
    Host* host_ = nullptr;
    float time_ = 0.0f;
    uint32_t rng_ = 0x4b15e7u;
    int randInt(int n) { rng_ = rng_ * 1664525u + 1013904223u; return n > 0 ? (int)((rng_ >> 8) % (uint32_t)n) : 0; }
    const Var* resolve(int i) const;
    bool builtin(int op, int input);
    void run();
};

} // namespace game
