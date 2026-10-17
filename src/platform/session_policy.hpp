#pragma once

namespace aether::win {

enum class SessionSignal {
    Lock,
    Unlock,
    Disconnect,
    Connect
};

struct SessionTransition {
    bool enteredPause = false;
    bool leftPause = false;
};

// Lock state and transport state clear independently. Only the outer pause
// edges may capture or consume the taskbar resume token.
class SessionPausePolicy {
public:
    [[nodiscard]] constexpr SessionTransition apply(SessionSignal signal) noexcept {
        const bool wasPaused = paused();
        switch (signal) {
        case SessionSignal::Lock:
            locked_ = true;
            break;
        case SessionSignal::Unlock:
            locked_ = false;
            break;
        case SessionSignal::Disconnect:
            disconnected_ = true;
            break;
        case SessionSignal::Connect:
            disconnected_ = false;
            break;
        }
        const bool isPaused = paused();
        return {!wasPaused && isPaused, wasPaused && !isPaused};
    }

    [[nodiscard]] constexpr bool paused() const noexcept {
        return locked_ || disconnected_;
    }

private:
    bool locked_ = false;
    bool disconnected_ = false;
};

} // namespace aether::win