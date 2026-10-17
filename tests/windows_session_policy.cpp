#include "platform/session_policy.hpp"
#include <iostream>

using namespace aether::win;

namespace {
int checks = 0;
int failures = 0;

void check(bool good, const char* name) {
    ++checks;
    std::cout << (good ? "PASS " : "FAIL ") << name << '\n';
    if (!good) ++failures;
}

void expectSignal(SessionPausePolicy& policy, SessionSignal signal,
                  bool paused, bool enteredPause, bool leftPause,
                  const char* name) {
    const auto transition = policy.apply(signal);
    check(policy.paused() == paused &&
          transition.enteredPause == enteredPause &&
          transition.leftPause == leftPause, name);
}
}

int main() {
    {
        SessionPausePolicy policy;
        check(!policy.paused(), "a new session policy begins active");
        expectSignal(policy, SessionSignal::Connect, false, false, false,
                     "a startup connect does not invent a resume edge");
        expectSignal(policy, SessionSignal::Unlock, false, false, false,
                     "a startup unlock does not invent a resume edge");
    }
    {
        SessionPausePolicy policy;
        expectSignal(policy, SessionSignal::Lock, true, true, false,
                     "the first lock captures one pause edge");
        expectSignal(policy, SessionSignal::Lock, true, false, false,
                     "a duplicate lock cannot overwrite the pause resume token");
        expectSignal(policy, SessionSignal::Unlock, false, false, true,
                     "unlock resumes a connected locked session once");
        expectSignal(policy, SessionSignal::Unlock, false, false, false,
                     "a duplicate unlock cannot repeat taskbar rearming");
    }
    {
        SessionPausePolicy policy;
        expectSignal(policy, SessionSignal::Lock, true, true, false,
                     "lock enters pause before transport disconnect");
        expectSignal(policy, SessionSignal::Disconnect, true, false, false,
                     "disconnect while locked does not create another pause edge");
        expectSignal(policy, SessionSignal::Connect, true, false, false,
                     "connect while still locked cannot rehide the taskbar");
        expectSignal(policy, SessionSignal::Connect, true, false, false,
                     "duplicate connect while locked remains paused");
        expectSignal(policy, SessionSignal::Unlock, false, false, true,
                     "only the final unlock resumes after reconnect");
    }
    {
        SessionPausePolicy policy;
        expectSignal(policy, SessionSignal::Disconnect, true, true, false,
                     "disconnect enters pause before the lock notification");
        expectSignal(policy, SessionSignal::Lock, true, false, false,
                     "lock while disconnected does not replace the first pause edge");
        expectSignal(policy, SessionSignal::Unlock, true, false, false,
                     "unlock while disconnected cannot rehide the taskbar");
        expectSignal(policy, SessionSignal::Unlock, true, false, false,
                     "duplicate unlock while disconnected remains paused");
        expectSignal(policy, SessionSignal::Connect, false, false, true,
                     "only the final reconnect resumes after unlock");
    }
    {
        SessionPausePolicy policy;
        expectSignal(policy, SessionSignal::Disconnect, true, true, false,
                     "a transport disconnect enters pause once");
        expectSignal(policy, SessionSignal::Disconnect, true, false, false,
                     "a repeated transport disconnect leaves the resume token intact");
        expectSignal(policy, SessionSignal::Connect, false, false, true,
                     "a normal transport reconnect resumes once");
        expectSignal(policy, SessionSignal::Connect, false, false, false,
                     "a repeated transport reconnect cannot rearm twice");
        expectSignal(policy, SessionSignal::Lock, true, true, false,
                     "a later independent lock starts a fresh pause interval");
        expectSignal(policy, SessionSignal::Unlock, false, false, true,
                     "a later independent unlock finishes that pause interval");
    }
    {
        SessionPausePolicy policy;
        expectSignal(policy, SessionSignal::Lock, true, true, false,
                     "lock enters the reversed recovery-order scenario");
        expectSignal(policy, SessionSignal::Disconnect, true, false, false,
                     "disconnect adds the second pause reason");
        expectSignal(policy, SessionSignal::Unlock, true, false, false,
                     "unlock cannot clear an outstanding disconnect reason");
        expectSignal(policy, SessionSignal::Disconnect, true, false, false,
                     "a repeated outstanding disconnect preserves pause");
        expectSignal(policy, SessionSignal::Connect, false, false, true,
                     "reconnect clears the final outstanding reason");
    }

    std::cout << (checks - failures) << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}