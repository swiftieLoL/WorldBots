#include "Globals/ObjectMgr.h"
#include "Player.h"
#include "SpellLearningUtils.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "SharedDefines.h"
#include "DataStores/DBCStores.h"
#include "DataStores/DBCStructure.h"
#include "Log.h"
#include "Diagnostics/BotTrace.h"
#include <algorithm>
#include <mutex>

namespace Helper
{
    static bool IsTrainableClassSpell(uint32_t spellId, uint8_t cls)
    {
        if (cls == 0 || cls > 31 || GetTalentSpellCost(spellId) != 0)
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || !SpellMgr::IsSpellValid(spellInfo, nullptr, false))
            return false;
        if (spellInfo->IsPassive() && spellInfo->SpellLevel == 0)
            return false;

        uint32_t classMask = 1u << (cls - 1);
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto it = bounds.first; it != bounds.second; ++it)
        {
            SkillLineAbilityEntry const* ability = it->second;
            if (ability && ability->AcquireMethod == 0 && (ability->ClassMask & classMask) != 0)
                return true;
        }

        return false;
    }

    void SpellLearningUtils::AutoLearnClassSpells(Player* bot, uint8_t& lastLearnedLevel)
    {
        if (!bot || !bot->IsInWorld()) return;

        uint8_t currentLevel = bot->GetLevel();
        if (lastLearnedLevel == currentLevel)
            return; // Already learned spells for current level!

        lastLearnedLevel = currentLevel; // Update debounce timer

        uint8_t cls = bot->GetClass();
        if (cls == 0) return;

        uint32_t learnedCount = 0;

        static std::unordered_map<uint8_t, std::vector<std::pair<uint32_t, uint32_t>>> s_trainableSpellsByClass;
        static std::once_flag s_spellIndexFlag;

        std::call_once(s_spellIndexFlag, []() {
            for (uint32_t spellId = 1; spellId < sSpellMgr->GetSpellInfoStoreSize(); ++spellId)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (!spellInfo) continue;

                for (uint8_t candidateClass = CLASS_WARRIOR; candidateClass <= CLASS_DRUID; ++candidateClass)
                {
                    if (IsTrainableClassSpell(spellId, candidateClass))
                    {
                        uint32_t reqLevel = std::max(spellInfo->SpellLevel, spellInfo->BaseLevel);
                        if (reqLevel == 0)
                            reqLevel = 1;
                        s_trainableSpellsByClass[candidateClass].push_back({ spellId, reqLevel });
                    }
                }
            }
        });

        auto it = s_trainableSpellsByClass.find(cls);
        if (it != s_trainableSpellsByClass.end())
        {
            for (const auto& entry : it->second)
            {
                uint32_t spellId = entry.first;
                uint32_t reqLevel = entry.second;
                if (reqLevel <= currentLevel && !bot->HasSpell(spellId))
                {
                    bot->LearnSpell(spellId, false);
                    learnedCount++;
                }
            }
        }

        if (learnedCount > 0 && Diagnostics::BotTrace::ShouldLog(bot))
        {
            TC_LOG_INFO("server", "[WorldBots] [SpellLearner] Bot '{}' (Class {}, Level {}) dynamically auto-learned {} class spells from sSpellMgr!",
                bot->GetName(), cls, currentLevel, learnedCount);
        }
    }
}
