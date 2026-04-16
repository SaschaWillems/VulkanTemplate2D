/*
* Copyright(C) 2024-2026 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <vector>
#include "Entity.hpp"
#include "Weapon.hpp"

namespace Game {
	namespace Entities {
		class Monster : public Entity {
		public:
			bool isBoss{ false };
			std::vector<Weapon> weapons;
			virtual void update(float delta) override;
		};
	}
}