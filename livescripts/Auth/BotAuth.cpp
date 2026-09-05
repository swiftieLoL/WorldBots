#include "BotAuth.h"
#include "Config/BotConfig.h"
#include "DatabaseEnv.h"
#include "Diagnostics/BotTrace.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "WorldPacket.h"
#include "World.h"
#include <algorithm>
#include <cstdio>

static std::vector<PendingBotInfo> s_pendingBots;
struct TrackedBotSession
{
    WorldSession* session = nullptr;
    bool owned = true;
};
static std::unordered_map<ObjectGuid, TrackedBotSession> s_botSessions;

namespace BotAuth
{
    SpawnSessionResult SpawnBotSession(uint32_t accountId, ObjectGuid guid, uint32_t attempt)
    {
        // A bot can be requested by both the factory and a lifecycle recovery
        // path. Only one login pipeline may exist for a GUID at a time.
        for (const auto& pending : s_pendingBots)
        {
            if (pending.guid == guid)
            {
                return pending.state && pending.state->cancelled.load(std::memory_order_acquire)
                    ? SpawnSessionResult::CancellationDraining
                    : SpawnSessionResult::AlreadyPending;
            }
        }

        Player* existing = ObjectAccessor::FindPlayer(guid);
        if (existing && existing->IsInWorld())
        {
            // Hot-reload support: Bot is already active in world, re-register session and queue pending login callback
            auto state = std::make_shared<PendingBotState>();
            state->callbackFired.store(true, std::memory_order_release);
            // The live session belongs to the world/session manager. Track it
            // for bot lookup, but never log it out or destroy it here.
            s_pendingBots.push_back({ existing->GetSession(), state, guid, accountId, attempt, 0, false });
            if (Diagnostics::BotTrace::ShouldLog(existing, Diagnostics::LogEvent::Normal))
            {
                TC_LOG_INFO("server", "[WorldBots] [Auth] Hot-reloaded active bot '{}' (GUID: {}) in world.", existing->GetName(), guid.GetCounter());
            }
            return SpawnSessionResult::Started;
        }

        // Older WorldBots builds inserted the Human starting bind for every
        // character. Remove that exact legacy row when it cannot be correct;
        // Player::_LoadHomeBind will synchronously select the race/class start
        // data and persist the proper bind as part of the native login path.
        // Direct execution guarantees the repair completes before the async
        // login query holder reads character_homebind.
        std::string homebindRepair = fmt::format(
            "DELETE hb FROM character_homebind hb "
            "INNER JOIN characters c ON c.guid = hb.guid "
            "WHERE hb.guid = {} "
            "AND (c.`race` <> 1 OR c.`class` = 6) "
            "AND hb.mapId = 0 AND hb.zoneId = 12 "
            "AND ABS(hb.posX + 8949.95) < 0.1 "
            "AND ABS(hb.posY + 132.493) < 0.1 "
            "AND ABS(hb.posZ - 83.5312) < 0.1",
            guid.GetCounter());
        CharacterDatabase.DirectExecute(homebindRepair.c_str());

        // Ensure any lingering session for this bot GUID is cleaned up before creating a new one
        RemoveBotSession(guid);

        WorldSession* botSession = new WorldSession(accountId, "", nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), Minutes(0), LOCALE_enUS, 0, false);
        botSession->m_timeOutTime = 2000000000;

        auto state = std::make_shared<PendingBotState>();
        s_pendingBots.push_back({ botSession, state, guid, accountId, attempt, 0, true });

        // LoginQueryHolder is private to TrinityCore's CharacterHandler.cpp,
        // so constructing a lookalike holder and casting it to that unrelated
        // type is undefined behavior. Populate the session's legitimate
        // character set asynchronously, then enter the native login opcode;
        // TrinityCore constructs and consumes its real holder internally.
        CharacterDatabasePreparedStatement* enumStatement = CharacterDatabase.GetPreparedStatement(
            sWorld->getBoolConfig(CONFIG_DECLINED_NAMES_USED)
                ? CHAR_SEL_ENUM_DECLINED_NAME : CHAR_SEL_ENUM);
        enumStatement->setUInt8(0, PET_SAVE_AS_CURRENT);
        enumStatement->setUInt32(1, accountId);
        botSession->GetQueryProcessor().AddCallback(
            CharacterDatabase.AsyncQuery(enumStatement).WithPreparedCallback(
                [botSession, state, guid](PreparedQueryResult result)
                {
                    if (state->cancelled.load(std::memory_order_acquire))
                    {
                        state->callbackFired.store(true, std::memory_order_release);
                        return;
                    }

                    botSession->HandleCharEnum(result);
                    state->loginDispatched.store(true, std::memory_order_release);
                    WorldPacket loginPacket(CMSG_PLAYER_LOGIN, 8);
                    loginPacket << guid;
                    botSession->HandlePlayerLoginOpcode(loginPacket);
                    if (!botSession->PlayerLoading())
                        state->callbackFired.store(true, std::memory_order_release);
                }));
        return SpawnSessionResult::Started;
    }

    void UpdatePendingSessions(uint32_t diff,
        std::function<void(Player* botPlayer, WorldSession* session)> onBotLoggedIn,
        std::function<void(uint32_t accountId, ObjectGuid guid, uint32_t attempt,
            const char* reason)> onRetryableFailure)
    {
        for (auto it = s_pendingBots.begin(); it != s_pendingBots.end(); )
        {
            WorldSession* sess = it->session;
            if (!sess || !it->state)
            {
                it = s_pendingBots.erase(it);
                continue;
            }

            it->elapsedMs += diff;
            if (it->elapsedMs >= Config::BotConfig::GetLoginTimeoutMs() &&
                !it->state->callbackFired.load(std::memory_order_acquire) &&
                !it->state->cancelled.exchange(true))
            {
                it->state->retryableFailure.store(true, std::memory_order_release);
                TC_LOG_ERROR("server", "[WorldBots] [Auth] Bot login timed out for GUID {}. Cancelling login pipeline.", it->guid.GetCounter());
            }

            if (it->state->cancelled.load(std::memory_order_acquire))
            {
                // Socketless sessions own their callback processors. Keep
                // draining them after cancellation so a ready enum/login
                // callback can reach the deterministic cleanup point.
                if (it->ownsSession &&
                    !it->state->callbackFired.load(std::memory_order_acquire))
                {
                    BotSessionFilter updater(sess);
                    sess->Update(diff, updater);
                    if (it->state->loginDispatched.load(std::memory_order_acquire) &&
                        !sess->PlayerLoading())
                        it->state->callbackFired.store(true, std::memory_order_release);
                }

                // The query callback owns the final cleanup point. Keeping the
                // entry until it fires avoids deleting a session while the DB
                // callback still captures it.
                if (it->state->callbackFired.load(std::memory_order_acquire))
                {
                    uint32_t accountId = it->accountId;
                    ObjectGuid guid = it->guid;
                    uint32_t attempt = it->attempt;
                    bool retryable = it->state->retryableFailure.load(std::memory_order_acquire);
                    if (it->ownsSession)
                    {
                        // A login callback may have completed immediately
                        // before shutdown marked the entry cancelled. Tear
                        // down a resulting player through the normal session
                        // lifecycle rather than deleting its session out from
                        // underneath the world object.
                        if (sess->GetPlayer())
                            sess->LogoutPlayer(true);
                        delete sess;
                    }
                    it = s_pendingBots.erase(it);
                    if (retryable && onRetryableFailure)
                        onRetryableFailure(accountId, guid, attempt, "login timeout");
                    continue;
                }

                ++it;
                continue;
            }

            // Adopted sessions are already driven by the world's session
            // loop. Updating them here as well can dispatch packets twice in
            // the same tick. Only module-owned socketless sessions need this
            // private update path.
            if (it->ownsSession)
            {
                BotSessionFilter updater(sess);
                sess->Update(diff, updater);
                if (it->state->loginDispatched.load(std::memory_order_acquire) &&
                    !sess->PlayerLoading())
                    it->state->callbackFired.store(true, std::memory_order_release);
            }

            if (sess->GetPlayer() && sess->GetPlayer()->IsInWorld())
            {
                Player* botPlayer = sess->GetPlayer();
                s_botSessions[botPlayer->GetGUID()] = { sess, it->ownsSession };

                if (onBotLoggedIn)
                {
                    onBotLoggedIn(botPlayer, sess);
                }

                it = s_pendingBots.erase(it);
                continue;
            }
            else if (it->state->callbackFired.load(std::memory_order_acquire))
            {
                uint32_t accountId = it->accountId;
                ObjectGuid guid = it->guid;
                uint32_t attempt = it->attempt;
                TC_LOG_ERROR("server", "[WorldBots] [Auth] Bot login failed for GUID {}. Removing pending session.", guid.GetCounter());
                if (it->ownsSession)
                    delete sess;
                it = s_pendingBots.erase(it);
                if (onRetryableFailure)
                    onRetryableFailure(accountId, guid, attempt, "login callback completed without a player");
                continue;
            }
            ++it;
        }
    }

    void CancelPendingSessions()
    {
        for (auto& pending : s_pendingBots)
        {
            if (pending.state)
                pending.state->cancelled.store(true, std::memory_order_release);
        }
    }

    SessionInfo GetBotSessionInfo(ObjectGuid guid)
    {
        auto it = s_botSessions.find(guid);
        if (it == s_botSessions.end())
            return {};
        return { it->second.session,
            it->second.owned ? SessionOwnership::Owned : SessionOwnership::Adopted };
    }

    WorldSession* GetBotSession(ObjectGuid guid)
    {
        return GetBotSessionInfo(guid).session;
    }

    SessionOwnership GetSessionOwnership(ObjectGuid guid)
    {
        return GetBotSessionInfo(guid).ownership;
    }

    void RemoveBotSession(ObjectGuid guid)
    {
        auto it = s_botSessions.find(guid);
        if (it == s_botSessions.end())
            return;

        WorldSession* session = it->second.session;
        bool owned = it->second.owned;
        s_botSessions.erase(it);

        // Socketless bot sessions are created by this module and are never
        // inserted into World::m_sessions. Keep their ownership here so every
        // successful login has one deterministic destruction point.
        if (owned && session)
        {
            if (session->GetPlayer())
                session->LogoutPlayer(true);
            delete session;
        }
    }

    uint32_t GetPendingLoginCount()
    {
        return static_cast<uint32_t>(s_pendingBots.size());
    }

    uint32_t GetOwnedSessionCount()
    {
        return static_cast<uint32_t>(std::count_if(s_botSessions.begin(), s_botSessions.end(),
            [](const auto& entry) { return entry.second.owned; }));
    }

    uint32_t GetAdoptedSessionCount()
    {
        return static_cast<uint32_t>(std::count_if(s_botSessions.begin(), s_botSessions.end(),
            [](const auto& entry) { return !entry.second.owned; }));
    }
}
