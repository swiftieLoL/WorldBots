#pragma once

#include "Helper/NpcFinder.h"
#include "Helper/MovementManager.h"
#include "Helper/Constants.h"
#include "Creature.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include <cstdint>
#include <functional>

namespace Helper
{
    enum class ApproachResult : uint8_t
    {
        Approaching,    // Movement issued towards target, caller should yield (return)
        Ready,          // Target in range and interaction prepared, caller should execute payload
        NotFound,       // No valid target found in search radius
        Invalid         // Target exists but is not currently interactable
    };

    // Finds a nearby creature by entry, checks interaction status,
    // issues movement if needed, and prepares creature interaction.
    // On Ready, outCreature is set and PrepareCreatureInteraction has been executed (unless skipPrepare is true).
    inline ApproachResult ApproachCreature(
        Player* bot, MovementManager* movement, uint32_t entry, ObjectGuid guid,
        float searchRadius, Creature*& outCreature,
        float interactionRange = Constants::QuestInteractionRange,
        bool skipPrepare = false,
        const std::function<bool(float, float, float)>& moveTo = {})
    {
        outCreature = guid && bot ? ObjectAccessor::GetCreature(*bot, guid) : nullptr;
        if (outCreature && entry != 0 && outCreature->GetEntry() != entry)
            outCreature = nullptr;
        if (!outCreature)
            outCreature = NpcUtils::FindNearbyCreatureByEntry(bot, entry, searchRadius);
        if (!outCreature || !outCreature->IsAlive())
            return ApproachResult::NotFound;

        InteractionStatus status = NpcUtils::GetInteractionStatus(bot, outCreature, interactionRange);
        if (status == InteractionStatus::NeedsMovement)
        {
            if (!movement)
                return ApproachResult::Invalid;

            bool moving = moveTo
                ? moveTo(outCreature->GetPositionX(), outCreature->GetPositionY(), outCreature->GetPositionZ())
                : movement->MoveTo(outCreature->GetPositionX(), outCreature->GetPositionY(), outCreature->GetPositionZ(),
                    BotMovementState::Moving, false);
            return moving ? ApproachResult::Approaching : ApproachResult::Invalid;
        }
        if (status == InteractionStatus::Ready)
        {
            if (movement)
                movement->Stop();
            if (!skipPrepare)
                NpcUtils::PrepareCreatureInteraction(bot, outCreature);
            return ApproachResult::Ready;
        }
        return ApproachResult::Invalid;
    }

    inline ApproachResult ApproachCreature(
        Player* bot, MovementManager* movement, uint32_t entry,
        float searchRadius, Creature*& outCreature,
        float interactionRange = Constants::QuestInteractionRange,
        bool skipPrepare = false,
        const std::function<bool(float, float, float)>& moveTo = {})
    {
        return ApproachCreature(bot, movement, entry, ObjectGuid::Empty,
            searchRadius, outCreature, interactionRange, skipPrepare, moveTo);
    }

    // Same pattern for GameObjects. On Ready, outGo is set and go->Use(bot) has been called (unless skipUse is true).
    inline ApproachResult ApproachGameObject(
        Player* bot, MovementManager* movement,
        uint32_t entry, ObjectGuid guid, float searchRadius,
        GameObject*& outGo, bool skipUse = false,
        const std::function<bool(float, float, float)>& moveTo = {})
    {
        Map* map = bot ? bot->GetMap() : nullptr;
        if (!map)
            return ApproachResult::Invalid;

        outGo = guid
            ? map->GetGameObject(guid)
            : bot->FindNearestGameObject(entry, searchRadius);
        if (!outGo)
            return ApproachResult::NotFound;

        InteractionStatus status = NpcUtils::GetInteractionStatus(bot, outGo);
        if (status == InteractionStatus::NeedsMovement)
        {
            if (!movement)
                return ApproachResult::Invalid;
            bool moving = moveTo
                ? moveTo(outGo->GetPositionX(), outGo->GetPositionY(),
                    outGo->GetPositionZ())
                : movement->MoveTo(outGo->GetPositionX(),
                    outGo->GetPositionY(), outGo->GetPositionZ(),
                    BotMovementState::Moving, false);
            return moving
                ? ApproachResult::Approaching : ApproachResult::Invalid;
        }
        if (status == InteractionStatus::Ready)
        {
            if (movement)
                movement->Stop();
            if (!skipUse)
                outGo->Use(bot);
            return ApproachResult::Ready;
        }
        return ApproachResult::Invalid;
    }
}
