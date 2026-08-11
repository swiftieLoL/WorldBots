#pragma once

#include "Globals/ObjectMgr.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Server/WorldSession.h"
#include "Log.h"
#include "BotRoster.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Factory
{
    struct BotCharacterCreateInfo : public CharacterCreateInfo
    {
        BotCharacterCreateInfo(std::string name, uint8 race, uint8 cls, uint8 gender = 0)
        {
            Name = name;
            Race = race;
            Class = cls;
            Gender = gender;
            Skin = 0;
            Face = 0;
            HairStyle = 0;
            HairColor = 0;
            FacialHair = 0;
            OutfitId = 0;
            CharCount = 0;
        }
    };

    struct DeferredBotSpawn
    {
        uint32_t accountId;
        ObjectGuid guid;
        uint32_t delayMs;
        uint32_t attempt = 0;
    };

    struct PendingBotProvision
    {
        uint32_t slot = 0;
        uint32_t totalSlots = 0;
        bool persistent = true;
        BotDefinition definition;
    };

    class BotFactory
    {
    public:
        // Initializes fresh bot characters, cleans legacy records, and enqueues staggered spawns
        static void InitializeBots(uint32_t botCount, bool verboseLogging);

        // Services deferred spawn timers using in-memory character cache validation
        static void ProcessDeferredSpawns(uint32_t diff);

        // Adds an initial or recovery login to the same bounded queue used by
        // factory-created bots.
        static void QueueBotLogin(uint32_t accountId, ObjectGuid guid,
            uint32_t delayMs = 0, uint32_t attempt = 0);

        // Requeues a failed asynchronous login using the configured retry policy.
        static void QueueLoginRetry(uint32_t accountId, ObjectGuid guid, uint32_t attempt,
            const char* reason);

        // Creates a fresh bot character using standard Botharry creation pipeline
        static ObjectGuid CreateFreshBotCharacter(std::string const& botName, uint32_t accountId,
            const BotDefinition& definition, bool verboseLogging);

        // Returns the creation and behavior profile assigned to a managed bot.
        static BotDefinition GetBotDefinition(std::string const& botName);
        static uint32_t GetPendingProvisionCount();
        static uint32_t GetPendingSpawnCount();

        // Helper string normalizers
        static std::string NormalizeBotName(std::string name);
        static std::string GenerateBotName(uint32_t index);
    };
}
