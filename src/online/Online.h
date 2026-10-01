// Clean-room reconstruction — online interface PLACEHOLDER.
// Intentionally empty of protocol logic. Networking is deferred; this only reserves the
// architectural seam so gameplay can later query an online service without depending on
// any concrete backend (platform/Demonware/etc.).
#pragma once

namespace online {

enum class ConnectionState { Offline, Connecting, Online, Error };

class IOnlineService {
public:
    virtual ~IOnlineService() = default;
    virtual ConnectionState state() const = 0;
    virtual void update(float dt) = 0;
};

// Null implementation: always offline. Keeps single-player/offline builds self-contained.
class NullOnlineService final : public IOnlineService {
public:
    ConnectionState state() const override { return ConnectionState::Offline; }
    void update(float) override {}
};

} // namespace online
