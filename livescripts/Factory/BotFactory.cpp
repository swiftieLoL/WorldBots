#include "Globals/ObjectMgr.h"
#include "BotFactory.h"
#include "Auth/BotAuth.h"
#include "Auth/BotLoginPolicy.h"
#include "Accounts/AccountMgr.h"
#include "Cache/CharacterCache.h"
#include "Config/BotConfig.h"
#include "DatabaseEnv.h"
#include "Diagnostics/BotTrace.h"
#include "Helper/InventoryUtils.h"
#include "Helper/ProgressionUtils.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Server/WorldSession.h"
#include "World.h"
#include "fmt/format.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <deque>
#include <random>
#include <string_view>
#include <unordered_map>

namespace Factory
{
    static std::vector<DeferredBotSpawn> s_deferredSpawns;
    static std::deque<PendingBotProvision> s_pendingProvisions;
    static std::unordered_map<std::string, BotDefinition> s_botDefinitions;
    static uint32_t s_startupGraceRemainingMs = 0;
    static uint32_t s_playerLoginGraceRemainingMs = 0;
    static uint32_t s_loginLaunchCooldownRemainingMs = 0;
    static uint32_t s_lastObservedPlayerSessionCount = 0;
    static bool s_pausedForPlayerLogin = false;
    static uint32_t s_playerQueueLogCooldownMs = 0;

    namespace
    {
        std::string GenerateAccountPassword()
        {
            constexpr std::string_view alphabet =
                "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
            std::random_device random;
            std::uniform_int_distribution<size_t> distribution(0, alphabet.size() - 1);
            std::string password;
            password.reserve(MAX_PASS_STR);
            for (size_t i = 0; i < MAX_PASS_STR; ++i)
                password.push_back(alphabet[distribution(random)]);
            return password;
        }

        bool IsManagedAccount(uint32_t accountId)
        {
            if (accountId == 0)
                return false;

            if (Config::BotConfig::ShouldMigrateLegacyCharacters() &&
                accountId == Config::BotConfig::GetBotAccountId())
                return true;

            std::string accountName;
            std::string accountEmail;
            return AccountMgr::GetName(accountId, accountName) &&
                AccountMgr::GetEmail(accountId, accountEmail) &&
                accountEmail == "WORLDBOTS@INVALID.LOCAL" &&
                HasDedicatedAccountPrefix(accountName, Config::BotConfig::GetDedicatedAccountPrefix());
        }

        uint32_t ResolveDedicatedAccount(uint32_t slot, ObjectGuid expectedBotGuid,
            std::string const& botName)
        {
            std::string accountName = GenerateDedicatedAccountName(
                Config::BotConfig::GetDedicatedAccountPrefix(), slot);
            uint32_t accountId = AccountMgr::GetId(accountName);
            bool created = false;

            if (accountId == 0 && Config::BotConfig::AutoCreateDedicatedAccounts())
            {
                AccountOpResult result = sAccountMgr->CreateAccount(
                    accountName, GenerateAccountPassword(), "worldbots@invalid.local");
                if (result != AccountOpResult::AOR_OK &&
                    result != AccountOpResult::AOR_NAME_ALREADY_EXIST)
                {
                    TC_LOG_ERROR("server", "[WorldBots] [Factory] Could not create dedicated account '{}' for slot {} (result {}).",
                        accountName, slot, static_cast<uint32_t>(result));
                    return 0;
                }
                accountId = AccountMgr::GetId(accountName);
                created = result == AccountOpResult::AOR_OK;
            }

            if (accountId == 0)
            {
                TC_LOG_ERROR("server", "[WorldBots] [Factory] Dedicated account '{}' does not exist and automatic account creation is disabled.",
                    accountName);
                return 0;
            }

            std::string accountEmail;
            if (!AccountMgr::GetEmail(accountId, accountEmail) ||
                accountEmail != "WORLDBOTS@INVALID.LOCAL")
            {
                TC_LOG_ERROR("server", "[WorldBots] [Factory] Refusing account-name collision '{}': the account is not marked as owned by WorldBots.",
                    accountName);
                return 0;
            }

            uint32_t characterCount = AccountMgr::GetCharactersCount(accountId);
            uint32_t expectedAccountId = expectedBotGuid
                ? sCharacterCache->GetCharacterAccountIdByGuid(expectedBotGuid) : 0;
            bool expectedBotOwnsAccount = expectedBotGuid && expectedAccountId == accountId;
            if ((!expectedBotOwnsAccount && characterCount != 0) ||
                (expectedBotOwnsAccount && characterCount != 1))
            {
                TC_LOG_ERROR("server", "[WorldBots] [Factory] Refusing dedicated account '{}' (ID {}) for bot '{}': expected an isolated empty account or exactly that one managed character, found {} character(s).",
                    accountName, accountId, botName, characterCount);
                return 0;
            }

            if (created && Diagnostics::BotTrace::ShouldLog(
                nullptr, Diagnostics::LogEvent::Normal))
                TC_LOG_INFO("server", "[WorldBots] [Factory] Created isolated account '{}' (ID {}) for bot slot {}.",
                    accountName, accountId, slot);
            return accountId;
        }

        bool MoveManagedCharacterToAccount(ObjectGuid guid, std::string const& botName,
            uint32_t oldAccountId, uint32_t newAccountId)
        {
            if (!guid || oldAccountId == 0 || newAccountId == 0)
                return false;
            if (!IsManagedAccount(oldAccountId))
            {
                TC_LOG_ERROR("server", "[WorldBots] [Factory] Refusing to move bot-named character '{}' (GUID {}) from unrecognized account {}. Set WorldBots.AccountId to the legacy owner only if this is an intentional migration.",
                    botName, guid.GetCounter(), oldAccountId);
                return false;
            }
            if (Player* active = ObjectAccessor::FindConnectedPlayer(guid))
            {
                TC_LOG_ERROR("server", "[WorldBots] [Factory] Cannot move managed character '{}' while it is connected.", botName);
                return false;
            }

            CharacterDatabasePreparedStatement* statement =
                CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_BY_GUID);
            statement->setUInt32(0, newAccountId);
            statement->setUInt32(1, guid.GetCounter());
            CharacterDatabase.DirectExecute(statement);
            sCharacterCache->UpdateCharacterAccountId(guid, newAccountId);
            sWorld->UpdateRealmCharCount(oldAccountId);
            sWorld->UpdateRealmCharCount(newAccountId);
            if (Diagnostics::BotTrace::ShouldLogGuid(
                static_cast<uint32_t>(guid.GetCounter()), Diagnostics::LogEvent::Normal))
            {
                TC_LOG_INFO("server", "[WorldBots] [Factory] Moved managed character '{}' (GUID {}) from account {} to isolated bot account {}.",
                    botName, guid.GetCounter(), oldAccountId, newAccountId);
            }
            return true;
        }
    }

    std::string BotFactory::NormalizeBotName(std::string name)
    {
        if (!name.empty())
        {
            name[0] = std::toupper(name[0]);
            for (size_t i = 1; i < name.length(); ++i)
                name[i] = std::tolower(name[i]);
        }
        return name;
    }

    std::string BotFactory::GenerateBotName(uint32_t index)
    {
        constexpr std::string_view baseName = "Botharry";
        if (index == 0)
            return std::string(baseName);

        static const char* const suffixes[] = {
            "alpha", "bravo", "charlie", "delta", "echo", "foxtrot",
            "golf", "hotel", "india", "juliet", "kilo", "lima",
            "mike", "november", "oscar", "papa", "quebec", "romeo",
            "sierra", "tango", "uniform", "victor", "whiskey", "xray",
            "yankee", "zulu"
        };

        if (index <= sizeof(suffixes) / sizeof(suffixes[0]))
        {
            // Preserve the recognizable NATO suffix while respecting the
            // client's 12-character player-name limit. The first four letters
            // are unique across this suffix table.
            std::string suffix = suffixes[index - 1];
            suffix.resize(std::min(suffix.size(), static_cast<size_t>(MAX_PLAYER_NAME - baseName.size())));
            return std::string(baseName) + suffix;
        }

        // Continue with an alphabetic base-26 sequence for larger bot pools.
        // Even UINT32_MAX needs only seven letters, keeping "Bot" + suffix
        // within MAX_PLAYER_NAME and valid under normal character-name rules.
        std::string encodedIndex;
        uint32_t value = index;
        while (value > 0)
        {
            --value;
            encodedIndex.push_back(static_cast<char>('a' + (value % 26)));
            value /= 26;
        }
        std::reverse(encodedIndex.begin(), encodedIndex.end());
        return std::string("Bot") + encodedIndex;
    }

    BotDefinition BotFactory::GetBotDefinition(std::string const& botName)
    {
        auto it = s_botDefinitions.find(NormalizeBotName(botName));
        return it == s_botDefinitions.end()
            ? Config::BotConfig::GetDefaultBotDefinition()
            : it->second;
    }

    uint32_t BotFactory::GetPendingProvisionCount()
    {
        return static_cast<uint32_t>(s_pendingProvisions.size());
    }

    uint32_t BotFactory::GetPendingSpawnCount()
    {
        return static_cast<uint32_t>(s_deferredSpawns.size());
    }

    uint32_t BotFactory::GetStartupGraceRemainingMs()
    {
        return s_startupGraceRemainingMs;
    }

    uint32_t BotFactory::GetLoginLaunchCooldownRemainingMs()
    {
        return s_loginLaunchCooldownRemainingMs;
    }

    uint32_t BotFactory::GetPlayerLoginGraceRemainingMs()
    {
        return s_playerLoginGraceRemainingMs;
    }

    bool BotFactory::IsPausedForPlayerLogin()
    {
        return s_pausedForPlayerLogin;
    }

    void BotFactory::QueueBotLogin(uint32_t accountId, ObjectGuid guid,
        uint32_t delayMs, uint32_t attempt)
    {
        auto existing = std::find_if(s_deferredSpawns.begin(), s_deferredSpawns.end(),
            [guid](DeferredBotSpawn const& pending) { return pending.guid == guid; });
        if (existing != s_deferredSpawns.end())
        {
            existing->accountId = accountId;
            existing->delayMs = delayMs;
            existing->attempt = attempt;
            return;
        }

        s_deferredSpawns.push_back({ accountId, guid, delayMs, attempt });
    }

    void BotFactory::QueueLoginRetry(uint32_t accountId, ObjectGuid guid, uint32_t attempt,
        const char* reason)
    {
        uint32_t maximumRetries = Config::BotConfig::GetLoginMaxRetries();
        if (attempt >= maximumRetries)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Factory] Giving up login for GUID {} after {} attempt(s): {}.",
                guid.GetCounter(), attempt + 1, reason ? reason : "unknown failure");
            return;
        }

        uint32_t delayMs = BotAuth::CalculateRetryDelayMs(attempt,
            Config::BotConfig::GetLoginRetryInitialDelayMs(),
            Config::BotConfig::GetLoginRetryMaxDelayMs());
        uint32_t nextAttempt = attempt + 1;

        QueueBotLogin(accountId, guid, delayMs, nextAttempt);

        TC_LOG_WARN("server", "[WorldBots] [Factory] Requeued login for GUID {} in {} ms (retry {}/{}): {}.",
            guid.GetCounter(), delayMs, nextAttempt, maximumRetries,
            reason ? reason : "unknown failure");
    }

    void BotFactory::ProcessDeferredSpawns(uint32_t diff)
    {
        s_startupGraceRemainingMs = s_startupGraceRemainingMs > diff
            ? s_startupGraceRemainingMs - diff : 0;
        s_playerLoginGraceRemainingMs = s_playerLoginGraceRemainingMs > diff
            ? s_playerLoginGraceRemainingMs - diff : 0;
        s_loginLaunchCooldownRemainingMs = s_loginLaunchCooldownRemainingMs > diff
            ? s_loginLaunchCooldownRemainingMs - diff : 0;
        s_playerQueueLogCooldownMs = s_playerQueueLogCooldownMs > diff
            ? s_playerQueueLogCooldownMs - diff : 0;

        uint32_t playerSessionCount = sWorld
            ? sWorld->GetActiveAndQueuedSessionCount() : 0;
        if (Config::BotConfig::PrioritizePlayerLogins() &&
            playerSessionCount > s_lastObservedPlayerSessionCount)
        {
            s_playerLoginGraceRemainingMs =
                Config::BotConfig::GetPlayerLoginGraceMs();
            if (Diagnostics::BotTrace::ShouldLog(nullptr, Diagnostics::LogEvent::Normal))
            {
                TC_LOG_INFO("server", "[WorldBots] [Factory] Detected a new real-player session; pausing new bot work for {} ms so player login can complete first.",
                    s_playerLoginGraceRemainingMs);
            }
        }
        s_lastObservedPlayerSessionCount = playerSessionCount;

        uint32_t queuedPlayerCount = sWorld ? sWorld->GetQueuedSessionCount() : 0;
        s_pausedForPlayerLogin = Config::BotConfig::PrioritizePlayerLogins() &&
            (s_playerLoginGraceRemainingMs != 0 || queuedPlayerCount != 0);
        if (BotAuth::ShouldPauseBotProvisioning(s_startupGraceRemainingMs,
            s_playerLoginGraceRemainingMs, queuedPlayerCount,
            Config::BotConfig::PrioritizePlayerLogins()))
        {
            if (queuedPlayerCount != 0 && s_playerQueueLogCooldownMs == 0)
            {
                if (Diagnostics::BotTrace::ShouldLog(nullptr, Diagnostics::LogEvent::Normal))
                {
                    TC_LOG_INFO("server", "[WorldBots] [Factory] Pausing new bot provisioning and login launches while {} player session(s) wait for the realm.",
                        queuedPlayerCount);
                }
                s_playerQueueLogCooldownMs = 5000;
            }
            return;
        }

        // Age every spawn that was already waiting before any budgeted work.
        // Provisioning or launching one bot may consume the whole task budget
        // (or deliberately break after a launch), but that must not freeze the
        // timers of entries later in the queue.
        for (DeferredBotSpawn& spawn : s_deferredSpawns)
            spawn.delayMs = spawn.delayMs > diff ? spawn.delayMs - diff : 0;

        const auto taskStarted = std::chrono::steady_clock::now();
        const auto taskBudget = std::chrono::milliseconds(Config::BotConfig::GetFactoryTaskBudgetMs());
        auto budgetExhausted = [&]() {
            return std::chrono::steady_clock::now() - taskStarted >= taskBudget;
        };
        auto enqueueDeferredSpawn = [&](uint32_t accId, ObjectGuid botGuid, uint32_t slot) {
            uint32_t spawnDelay = Config::BotConfig::GetBaseSpawnDelayMs() +
                (slot * Config::BotConfig::GetSpawnDelayStepMs());
            s_deferredSpawns.push_back({ accId, botGuid, spawnDelay });
        };

        uint32_t operations = Config::BotConfig::GetFactoryOperationsPerTick();
        while (operations-- > 0 && !s_pendingProvisions.empty() && !budgetExhausted())
        {
            PendingBotProvision provision = s_pendingProvisions.front();
            s_pendingProvisions.pop_front();

            std::string botName = NormalizeBotName(GenerateBotName(provision.slot));
            s_botDefinitions[botName] = provision.definition;
            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(botName);
            uint32_t accountId = Config::BotConfig::UseDedicatedAccounts()
                ? ResolveDedicatedAccount(provision.slot, guid, botName)
                : Config::BotConfig::GetBotAccountId();
            if (accountId == 0)
                continue;

            if (guid)
            {
                uint32_t existingAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
                if (existingAccount != accountId &&
                    !MoveManagedCharacterToAccount(guid, botName, existingAccount, accountId))
                    continue;
            }

            if (provision.persistent && guid)
            {
                CharacterCacheEntry const* cacheEntry = sCharacterCache->GetCharacterCacheByGuid(guid);
                if (cacheEntry && !Config::BotConfig::IsBotClassAllowed(cacheEntry->Class))
                {
                    TC_LOG_WARN("server", "[WorldBots] [Factory] Existing persistent bot '{}' has disallowed class {} (e.g. Death Knight disabled); deleting and recreating as allowed class {}.",
                        botName, static_cast<uint32_t>(cacheEntry->Class), static_cast<uint32_t>(provision.definition.playerClass));
                }
                else
                {
                    enqueueDeferredSpawn(accountId, guid, provision.slot);
                    continue;
                }
            }

            if (guid)
            {
                Player* inWorldPlayer = ObjectAccessor::FindPlayer(guid);
                if (inWorldPlayer)
                {
                    TC_LOG_WARN("server", "[WorldBots] [Factory] Bot '{}' is still active during provisioning; preserving it instead of recreating it.",
                        botName);
                    enqueueDeferredSpawn(accountId, guid, provision.slot);
                    continue;
                }

                Player::DeleteFromDB(guid, accountId, false, true);
                sCharacterCache->DeleteCharacterCacheEntry(guid, botName);
                sWorld->UpdateRealmCharCount(accountId);
            }

            guid = CreateFreshBotCharacter(botName, accountId, provision.definition,
                Diagnostics::BotTrace::IsGlobalVerbose());
            if (!guid)
                continue;

            enqueueDeferredSpawn(accountId, guid, provision.slot);
        }

        for (auto it = s_deferredSpawns.begin();
             it != s_deferredSpawns.end() && !budgetExhausted(); )
        {
            if (it->delayMs == 0)
            {
                if (!BotAuth::CanLaunchBotLogin(s_loginLaunchCooldownRemainingMs,
                    BotAuth::GetPendingLoginCount(),
                    Config::BotConfig::GetMaxConcurrentLogins()))
                {
                    it->delayMs = 0;
                    ++it;
                    continue;
                }

                if (!sCharacterCache->GetCharacterAccountIdByGuid(it->guid))
                {
                    TC_LOG_ERROR("server", "[WorldBots] [Factory] Deferred spawn FAILED: GUID {} not in character cache.", it->guid.GetCounter());
                    it = s_deferredSpawns.erase(it);
                    continue;
                }

                BotAuth::SpawnSessionResult spawnResult =
                    BotAuth::SpawnBotSession(it->accountId, it->guid, it->attempt);
                if (spawnResult == BotAuth::SpawnSessionResult::Started ||
                    spawnResult == BotAuth::SpawnSessionResult::AlreadyPending)
                {
                    bool launched = spawnResult == BotAuth::SpawnSessionResult::Started;
                    it = s_deferredSpawns.erase(it);
                    if (launched)
                    {
                        s_loginLaunchCooldownRemainingMs =
                            Config::BotConfig::GetLoginLaunchIntervalMs();
                        // Enforce a launch-rate limit as well as the concurrent
                        // pipeline cap. This prevents a batch of fast callbacks
                        // from admitting another burst in the same world tick.
                        break;
                    }
                }
                else if (spawnResult == BotAuth::SpawnSessionResult::CancellationDraining)
                {
                    // Reinitialization leaves cancelled DB callbacks alive
                    // until they reach their safe cleanup point. Retain the
                    // replacement request without consuming a retry attempt.
                    it->delayMs = std::max<uint32_t>(diff, 100u);
                    ++it;
                }
                else
                {
                    uint32_t accountId = it->accountId;
                    ObjectGuid guid = it->guid;
                    uint32_t attempt = it->attempt;
                    it = s_deferredSpawns.erase(it);
                    QueueLoginRetry(accountId, guid, attempt, "login pipeline initialization failed");
                    // Requeueing can reallocate the vector and invalidate the
                    // current iterator. Resume processing on the next tick.
                    break;
                }
            }
            else
            {
                ++it;
            }
        }
    }

    ObjectGuid BotFactory::CreateFreshBotCharacter(std::string const& botName, uint32_t accountId,
        const BotDefinition& definition, bool verboseLogging)
    {
        if (!Config::BotConfig::IsBotClassAllowed(definition.playerClass))
        {
            TC_LOG_ERROR("server", "[WorldBots] [Factory] Refusing to create bot '{}' with disabled class {}.",
                botName, static_cast<uint32_t>(definition.playerClass));
            return ObjectGuid::Empty;
        }

        bool validName = !botName.empty() && botName.size() <= MAX_PLAYER_NAME &&
            std::all_of(botName.begin(), botName.end(), [](unsigned char c) { return std::isalpha(c) != 0; });
        if (!validName)
        {
            TC_LOG_ERROR("server", "[WorldBots] [Factory] Refusing to create invalid bot name '{}' (length {}; maximum is {} alphabetic characters)",
                botName, botName.size(), MAX_PLAYER_NAME);
            return ObjectGuid::Empty;
        }

        ObjectGuid::LowType lowGuid = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();
        BotCharacterCreateInfo createInfo(
            botName,
            definition.race,
            definition.playerClass,
            definition.gender);

        WorldSession* createSession = new WorldSession(accountId, "", nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), Minutes(0), LOCALE_enUS, 0, false);
        Player* newPlayer = new Player(createSession);
        if (!newPlayer->Create(lowGuid, &createInfo))
        {
            TC_LOG_ERROR("server", "[WorldBots] [Factory] BotFactory: Failed to create character '{}'", botName);
            delete newPlayer;
            delete createSession;
            return ObjectGuid::Empty;
        }

        newPlayer->SetAtLoginFlag(AT_LOGIN_NONE);
        Helper::InventoryUtils::EnsureStarterBags(newPlayer,
            Config::BotConfig::GetStarterBagItemId(),
            Config::BotConfig::GetStarterBagCount());
        Helper::ProgressionUtils::EnsureFactionFlightPathsLearned(newPlayer);
        newPlayer->SaveToDB(true);

        sCharacterCache->AddCharacterCacheEntry(newPlayer->GetGUID(), accountId, botName,
            newPlayer->GetNativeGender(), newPlayer->GetRace(), newPlayer->GetClass(), newPlayer->GetLevel());
        sWorld->UpdateRealmCharCount(accountId);

        ObjectGuid guid = newPlayer->GetGUID();

        if (verboseLogging || Diagnostics::BotTrace::ShouldLogGuid(
            static_cast<uint32_t>(lowGuid), Diagnostics::LogEvent::Normal))
        {
            TC_LOG_INFO("server", "[WorldBots] [Factory] BotFactory: Created fresh character '{}' via Player::Create (GUID: {}, Account: {})", botName, lowGuid, accountId);
        }

        createSession->SetPlayer(nullptr);
        newPlayer->CleanupsBeforeDelete();
        delete newPlayer;
        delete createSession;

        return guid;
    }

    void BotFactory::InitializeBots(uint32_t botCount, bool /*verboseLogging*/)
    {
        uint32_t maxBotCount = Config::BotConfig::GetMaxBotCount();
        if (botCount > maxBotCount)
        {
            TC_LOG_WARN("server", "[WorldBots] [Factory] Requested {} bots; clamping to configured WorldBots.MaxBotCount {}.",
                botCount, maxBotCount);
            botCount = maxBotCount;
        }
        s_deferredSpawns.clear();
        s_pendingProvisions.clear();
        s_botDefinitions.clear();
        s_startupGraceRemainingMs = Config::BotConfig::GetFactoryStartupGraceMs();
        s_playerLoginGraceRemainingMs = 0;
        s_loginLaunchCooldownRemainingMs = 0;
        s_lastObservedPlayerSessionCount = sWorld
            ? sWorld->GetActiveAndQueuedSessionCount() : 0;
        if (Config::BotConfig::PrioritizePlayerLogins() &&
            s_lastObservedPlayerSessionCount != 0)
            s_playerLoginGraceRemainingMs = Config::BotConfig::GetPlayerLoginGraceMs();
        s_pausedForPlayerLogin = false;
        s_playerQueueLogCooldownMs = 0;

        bool saveProgress = Config::BotConfig::ShouldSaveBotProgress();
        uint32_t persistentQuota = saveProgress
            ? (botCount * Config::BotConfig::GetPersistentBotPercent() + 99) / 100
            : 0;
        std::vector<BotDefinition> roster = Config::BotConfig::GetBotRoster();
        BotDefinition fallback = Config::BotConfig::GetDefaultBotDefinition();

        TC_LOG_INFO("server", "[WorldBots] [Factory] Queued {} bot slot(s): {} persistent, {} disposable, {} account mode, {} operation(s) per factory tick, roster pattern length {}, startup grace {} ms, login launch interval {} ms.",
            botCount, persistentQuota, botCount - persistentQuota,
            Config::BotConfig::UseDedicatedAccounts() ? "dedicated" : "shared",
            Config::BotConfig::GetFactoryOperationsPerTick(), roster.empty() ? 1 : roster.size(),
            s_startupGraceRemainingMs, Config::BotConfig::GetLoginLaunchIntervalMs());

        for (uint32_t i = 0; i < botCount; ++i)
        {
            BotDefinition definition = SelectBotDefinition(roster, i, fallback);
            s_botDefinitions[NormalizeBotName(GenerateBotName(i))] = definition;
            s_pendingProvisions.push_back({ i, i < persistentQuota, definition });
        }
    }
}
