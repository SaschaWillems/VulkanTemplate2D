/*
* Copyright(C) 2024-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "Entity.hpp"

namespace Game {
	namespace Entities {
		class Player : public Entity {
		public:
			float experience;
			uint32_t level;
			float criticalChance{ 5.0f };
			float criticalDamageMultiplier{ 1.5f };
			float stamina{ 2.5f };
			float maxStamina{ 2.5f };
			float pickupDistance{ 3.0f };
			void addExperience(float value);
		};
	}
}