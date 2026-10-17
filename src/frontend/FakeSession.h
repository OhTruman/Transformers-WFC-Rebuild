// DEV TOOL / tests - a scripted, recording ISession with no network (docs/handoff/ONLINE_MP_FRONTEND_TESTS.md section 2).
// The test writes the session side of the conversation as a script: each entry fires at a frontend time, or after the
// flow has made a given call (e.g. HostOpened one frame after host()). Every call the flow makes is recorded with its
// arguments and time, so a test asserts what the flow sent ("requestStart was not called by a member"). Time is the
// frontend update time passed to poll(): "nothing arrives for 50 s" = updating 3000 times at 1/60 s.
// Used by wfc_frontend_tests (S1-S24) and, behind WFC_FAKESESSION=1 in development builds only, by the frontend
// script runner's session:<event> step for capture runs.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "frontend/Session.h"

namespace frontend {

class FakeSession : public ISession {
public:
    struct Call {
        std::string fn;                     // "host", "join", "cancelJoin", "leave", "publishLobby", "setMemberInfo",
                                            // "requestStart", "reportLoaded", "kick", "setVoiceSettings", "setPushToTalk", "setMuted"
        double time = 0.0;
        std::string arg;                    // join target / kicked member / "simId,loadHash"
        SessionHostSettings hostSettings;   // host()
        SessionLobby lobby;                 // publishLobby()
        SessionMemberInfo info;             // setMemberInfo()
    };
    struct Step {
        double at = -1.0;                   // fire at this frontend time (>= 0), or
        std::string afterCall;              // fire on the first poll after this call was recorded ("host", ...)
        double delay = 0.0;                 // ... plus this many seconds
        SessionEvent event;
        bool fired = false;                 // set by poll()
    };

    FakeSession() = default;
    explicit FakeSession(std::vector<Step> script) : script_(std::move(script)) {}
    void add(Step s) { script_.push_back(std::move(s)); }
    // Inject at once (the script runner's session:<event> step).
    void inject(SessionEvent e) { injected_.push_back(std::move(e)); }

    const std::vector<Call>& calls() const { return calls_; }
    int count(const std::string& fn) const { int n = 0; for (const Call& c : calls_) n += c.fn == fn; return n; }
    const Call* last(const std::string& fn) const {
        for (size_t i = calls_.size(); i-- > 0;) if (calls_[i].fn == fn) return &calls_[i];
        return nullptr;
    }

    // ---- ISession ----
    void host(const SessionHostSettings& s) override { Call c = mk("host"); c.hostSettings = s; calls_.push_back(c); }
    void join(const std::string& target) override { Call c = mk("join"); c.arg = target; calls_.push_back(c); }
    void cancelJoin() override { calls_.push_back(mk("cancelJoin")); }
    void leave() override { calls_.push_back(mk("leave")); }
    void publishLobby(const SessionLobby& l) override { Call c = mk("publishLobby"); c.lobby = l; calls_.push_back(c); }
    void setMemberInfo(const SessionMemberInfo& i) override { Call c = mk("setMemberInfo"); c.info = i; calls_.push_back(c); }
    void requestStart() override { calls_.push_back(mk("requestStart")); }
    void setLaunch(const std::string& url, uint32_t seed, const SessionLobby&) override { Call c = mk("setLaunch"); c.arg = url + "|" + std::to_string(seed); calls_.push_back(c); }
    void reportLoaded(uint64_t simId, uint64_t loadHash) override {
        Call c = mk("reportLoaded"); c.arg = std::to_string(simId) + "," + std::to_string(loadHash); calls_.push_back(c);
        held_ = true;   // the World waits for AllLoaded (as the LockstepLayer's start tick)
    }
    bool simHeld() const override { return held_; }
    void kick(const SessionMemberId& m, SessionKickReason) override { Call c = mk("kick"); c.arg = m.fingerprint; calls_.push_back(c); }
    void setVoiceSettings(const SessionVoiceSettings& v) override {
        Call c = mk("setVoiceSettings"); c.arg = std::to_string((int)v.mode) + "," + std::to_string((int)v.audience) + "," + v.device + "," + std::to_string(v.volume);
        calls_.push_back(c);
    }
    void setPushToTalk(bool held) override { Call c = mk("setPushToTalk"); c.arg = held ? "1" : "0"; calls_.push_back(c); }
    float mic = 0.0f;   // scripted Mic Test level
    float micLevel() const override { return mic; }
    void setMuted(const SessionMemberId& m, bool muted) override { Call c = mk("setMuted"); c.arg = m.fingerprint + (muted ? ",1" : ",0"); calls_.push_back(c); }

    std::vector<SessionEvent> poll(double now) override {
        now_ = now;
        std::vector<SessionEvent> out;
        for (SessionEvent& e : injected_) { e.time = now; out.push_back(std::move(e)); }
        injected_.clear();
        for (Step& s : script_) {
            if (s.fired) continue;
            bool due = false;
            if (s.at >= 0.0) due = now >= s.at;
            else if (const Call* c = s.afterCall.empty() ? nullptr : last(s.afterCall)) due = now >= c->time + s.delay;
            if (!due) continue;
            s.fired = true;
            SessionEvent e = s.event;
            e.time = now;
            out.push_back(std::move(e));
        }
        for (const SessionEvent& e : out) if (e.type == SessionEvent::Type::AllLoaded) held_ = false;
        return out;
    }
    bool scriptDone() const { for (const Step& s : script_) if (!s.fired) return false; return injected_.empty(); }

private:
    Call mk(const char* fn) const { Call c; c.fn = fn; c.time = now_; return c; }
    std::vector<Step> script_;
    std::vector<SessionEvent> injected_;
    std::vector<Call> calls_;
    bool held_ = false;
    double now_ = 0.0;
};

} // namespace frontend
