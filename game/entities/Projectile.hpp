/*
* Copyright(C) 2024-2026 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <optional>
#include "Entity.hpp"

namespace Game {
	namespace Entities {

		enum class ProjectileType {
			Directional = 0,
			Homing = 1
		};

		class Projectile : public Entity {
		public:
			ProjectileType type{ ProjectileType::Directional };
			// For homing projectiles
			std::optional<Entity> target{ std::nullopt };
			float damage;
			// @todo: better name
			float life;
			glm::vec3 lightColor{};
		};
	}
}