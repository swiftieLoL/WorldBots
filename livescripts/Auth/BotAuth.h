#pragma once

#include "WorldSession.h"
#include <cstdint>
#include <atomic>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

class Player;

struct PendingBotState
{
    std::atomic_bool callbackFired{ false };
    std::atomic_bool cancelled{ false };
    std::atomic_bool retryableFailure{ false };
    std::atomic_bool loginDispatched{ false };
};

struct PendingBotInfo
{
    WorldSession* session;
    std::shared_ptr<PendingBotState> state;
    ObjectGuid guid;
    uint32_t accountId = 0;
    uint32_t attempt = 0;
    uint32_t elapsedMs = 0;
    bool ownsSession = true;
};

// Filter used by WorldSession::Update for networkless bot sessions
class BotSessionFilter : public PacketFilter
{
public:
    explicit BotSessionFilter(WorldSession* pSession) : PacketFilter(pSession) {}
    bool Process(WorldPacket*) override { return true; }
};

namespace BotAuth
{
    enum class SpawnSessionResult : uint8_t
    {
        Started,
        AlreadyPending,
        CancellationDraining,
        Failed
    };

    enum class SessionOwnership : uint8_t
    {
        None,
        Owned,
        Adopted
    };

    struct SessionInfo
    {
        WorldSession* session = nullptr;
        SessionOwnership ownership = SessionOwnership::None;
    };

    // Spawns a socketless WorldSession for the given accountId and character GUID
    SpawnSessionResult SpawnBotSession(uint32_t accountId, ObjectGuid guid, uint32_t attempt = 0);

    // Updates pending bot login sessions and invokes callback when a bot player enters the world
    void UpdatePendingSessions(uint32_t diff,
        std::function<void(Player* botPlayer, WorldSession* session)> onBotLoggedIn,
        std::function<void(uint32_t accountId, ObjectGuid guid, uint32_t attempt,
            const char* reason)> onRetryableFailure = {});

    // Cancels login pipelines during runtime shutdown/reinitialization. The
    // session remains owned by the pending entry until its DB callback fires.
    void CancelPendingSessions();

    // Session tracking helpers
    SessionInfo GetBotSessionInfo(ObjectGuid guid);
    WorldSession* GetBotSession(ObjectGuid guid);
    SessionOwnership GetSessionOwnership(ObjectGuid guid);
    void RemoveBotSession(ObjectGuid guid);
    uint32_t GetPendingLoginCount();
    uint32_t GetOwnedSessionCount();
    uint32_t GetAdoptedSessionCount();
}
