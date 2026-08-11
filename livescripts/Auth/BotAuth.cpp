#include "Globals/ObjectMgr.h"
#include "BotAuth.h"
#include "Config/BotConfig.h"
#include "ObjectAccessor.h"
#include "World.h"
#include <cstdio>

class LoginQueryHolder;

static std::vector<PendingBotInfo> s_pendingBots;
struct TrackedBotSession
{
    WorldSession* session = nullptr;
    bool owned = true;
};
static std::unordered_map<ObjectGuid, TrackedBotSession> s_botSessions;

bool PlayerbotLoginQueryHolder::Initialize()
{
    SetSize(MAX_PLAYER_LOGIN_QUERY);

    bool res = true;
    ObjectGuid::LowType lowGuid = m_guid.GetCounter();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_FROM, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GROUP_MEMBER);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_GROUP, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_INSTANCE);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_BOUND_INSTANCES, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_AURAS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_AURAS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_SPELL);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SPELLS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_QUEST_STATUS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS_DAILY);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_DAILY_QUEST_STATUS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS_WEEKLY);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_WEEKLY_QUEST_STATUS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS_MONTHLY);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_MONTHLY_QUEST_STATUS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS_SEASONAL);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SEASONAL_QUEST_STATUS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_REPUTATION);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_REPUTATION, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_INVENTORY);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_INVENTORY, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_ACTIONS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_ACTIONS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_MAIL);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_MAILS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_MAILITEMS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_MAIL_ITEMS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_SOCIALLIST);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SOCIAL_LIST, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_HOMEBIND);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_HOME_BIND, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_SPELLCOOLDOWNS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SPELL_COOLDOWNS, stmt);

    if (sWorld->getBoolConfig(CONFIG_DECLINED_NAMES_USED))
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_DECLINEDNAMES);
        stmt->setUInt32(0, lowGuid);
        res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_DECLINED_NAMES, stmt);
    }

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GUILD_MEMBER);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_GUILD, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_ARENAINFO);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_ARENA_INFO, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_ACHIEVEMENTS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_ACHIEVEMENTS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_CRITERIAPROGRESS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_CRITERIA_PROGRESS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_EQUIPMENTSETS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_EQUIPMENT_SETS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_BGDATA);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_BG_DATA, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_GLYPHS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_GLYPHS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_TALENTS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_TALENTS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PLAYER_ACCOUNT_DATA);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_ACCOUNT_DATA, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_SKILLS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SKILLS, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_RANDOMBG);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_RANDOM_BG, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_BANNED);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_BANNED, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUSREW);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_QUEST_STATUS_REW, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CORPSE_LOCATION);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_CORPSE_LOCATION, stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_PETS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_PET_SLOTS, stmt);

    return res;
}

namespace BotAuth
{
    bool SpawnBotSession(uint32_t accountId, ObjectGuid guid, uint32_t attempt)
    {
        // A bot can be requested by both the factory and a lifecycle recovery
        // path. Only one login pipeline may exist for a GUID at a time.
        for (const auto& pending : s_pendingBots)
        {
            if (pending.guid == guid)
                return true;
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
            TC_LOG_INFO("server", "[WorldBots] [Auth] Hot-reloaded active bot '{}' (GUID: {}) in world.", existing->GetName(), guid.GetCounter());
            return true;
        }

        // Ensure character_homebind record exists in DB for this bot GUID.
        // NOTE: INSERT IGNORE is lightweight and idempotent. Synchronous execution
        // is acceptable here as it runs once per bot login, not in a hot loop.
        std::string homebindQuery = fmt::format(
            "INSERT IGNORE INTO character_homebind (guid, mapId, zoneId, posX, posY, posZ) VALUES ({}, 0, 12, -8949.95, -132.493, 83.5312)",
            guid.GetCounter());
        CharacterDatabase.Execute(homebindQuery.c_str());

        std::shared_ptr<PlayerbotLoginQueryHolder> holder = std::make_shared<PlayerbotLoginQueryHolder>(accountId, guid);
        if (!holder->Initialize())
        {
            TC_LOG_ERROR("server", "[WorldBots] [Auth] Failed to initialize login query holder for bot guid {}", guid.GetCounter());
            return false;
        }

        WorldSession* botSession = new WorldSession(accountId, "", nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), Minutes(0), LOCALE_enUS, 0, false);
        botSession->m_timeOutTime = 2000000000;

        auto state = std::make_shared<PendingBotState>();
        s_pendingBots.push_back({ botSession, state, guid, accountId, attempt, 0, true });

        botSession->AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(std::static_pointer_cast<SQLQueryHolder<CharacterDatabaseConnection>>(holder))).AfterComplete([botSession, state](SQLQueryHolderBase const& queryHolder)
        {
            if (!state->cancelled.load(std::memory_order_acquire))
            {
                PlayerbotLoginQueryHolder const& holderRef = static_cast<PlayerbotLoginQueryHolder const&>(queryHolder);
                const LoginQueryHolder& tcHolder = reinterpret_cast<const LoginQueryHolder&>(holderRef);
                botSession->HandlePlayerLogin(tcHolder);
            }
            state->callbackFired.store(true, std::memory_order_release);
        });
        return true;
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
                        delete sess;
                    it = s_pendingBots.erase(it);
                    if (retryable && onRetryableFailure)
                        onRetryableFailure(accountId, guid, attempt, "login timeout");
                    continue;
                }

                ++it;
                continue;
            }

            BotSessionFilter updater(sess);
            sess->Update(diff, updater);

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

    WorldSession* GetBotSession(ObjectGuid guid)
    {
        auto it = s_botSessions.find(guid);
        if (it != s_botSessions.end())
        {
            return it->second.session;
        }
        return nullptr;
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
}
