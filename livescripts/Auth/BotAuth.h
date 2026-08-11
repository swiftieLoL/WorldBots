#pragma once

#include "Globals/ObjectMgr.h"
#include "DatabaseEnv.h"
#include "QueryHolder.h"
#include "WorldSession.h"
#include "Player.h"
#include "Log.h"
#include <cstdint>
#include <atomic>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

// Query holder for loading bot character data from character database during login
class PlayerbotLoginQueryHolder : public SQLQueryHolder<CharacterDatabaseConnection>
{
private:
    uint32 m_accountId;
    ObjectGuid m_guid;
public:
    PlayerbotLoginQueryHolder(uint32 accountId, ObjectGuid guid)
        : m_accountId(accountId), m_guid(guid)
    {
    }

    ObjectGuid GetGuid() const { return m_guid; }
    uint32 GetAccountId() const { return m_accountId; }

    bool Initialize();
};

struct PendingBotState
{
    std::atomic_bool callbackFired{ false };
    std::atomic_bool cancelled{ false };
    std::atomic_bool retryableFailure{ false };
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
    // Shared account ID for bot-created characters
    constexpr uint32 BOT_ACCOUNT_ID = 1;

    // Spawns a socketless WorldSession for the given accountId and character GUID
    bool SpawnBotSession(uint32_t accountId, ObjectGuid guid, uint32_t attempt = 0);

    // Updates pending bot login sessions and invokes callback when a bot player enters the world
    void UpdatePendingSessions(uint32_t diff,
        std::function<void(Player* botPlayer, WorldSession* session)> onBotLoggedIn,
        std::function<void(uint32_t accountId, ObjectGuid guid, uint32_t attempt,
            const char* reason)> onRetryableFailure = {});

    // Cancels login pipelines during runtime shutdown/reinitialization. The
    // session remains owned by the pending entry until its DB callback fires.
    void CancelPendingSessions();

    // Session tracking helpers
    WorldSession* GetBotSession(ObjectGuid guid);
    void RemoveBotSession(ObjectGuid guid);
    uint32_t GetPendingLoginCount();
}
