// PC EXTENSION - the online flows of GameFlow (docs/handoff/ONLINE_MP_FLOWS.md, ONLINE_MP_FRONTEND_TESTS.md). Only active
// with a session set (GameFlow::setSession); without one GameFlow is today's offline frontend. The session layer behind
// ISession (Session.h) is Systems'; tests drive these flows with FakeSession.
#include "frontend/GameFlow.h"

#include "frontend/FlowTrace.h"

namespace frontend {

void GameFlow::onlineTick() {
    for (const SessionEvent& e : session_->poll(clock_)) onSessionEvent(e);
}

void GameFlow::onSessionEvent(const SessionEvent& e) {
    FlowTrace::emit("online.event", {{"type", sessionEventName(e.type)}, {"role", sessionRoleName(role_)}});
}

} // namespace frontend
