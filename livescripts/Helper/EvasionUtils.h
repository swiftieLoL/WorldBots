#pragma once

#include "Player.h"
#include "Map.h"
#include "G3D/Vector3.h"
#include "Helper/MathUtils.h"
#include <cmath>

namespace Helper
{
    class EvasionUtils
    {
    public:
        static G3D::Vector3 CalculateRandomEvasionPoint(Player* bot, float minDistance = 4.0f, float maxDistance = 8.0f)
        {
            if (!bot || !bot->IsInWorld()) return G3D::Vector3(0.0f, 0.0f, 0.0f);

            float curX = bot->GetPositionX();
            float curY = bot->GetPositionY();
            float curZ = bot->GetPositionZ();

            float evadX = curX;
            float evadY = curY;
            Helper::GetRandomPointInAnnulus(curX, curY, minDistance, maxDistance, evadX, evadY);
            float evadZ = curZ;

            if (bot->GetMap())
            {
                float mz = bot->GetMap()->GetHeight(bot->GetPhaseMask(), evadX, evadY, curZ, true);
                if (mz > -500.0f && !std::isnan(mz)) evadZ = mz;
            }

            return G3D::Vector3(evadX, evadY, evadZ);
        }
    };
}
