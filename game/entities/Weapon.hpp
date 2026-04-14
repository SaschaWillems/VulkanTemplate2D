/*
* Copyright(C) 2024-2026 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdint.h>
#include <string>

// @todo: enums for weapon types?

namespace Game {

	enum class WeaponType {
		Projectile = 0,
		ProjectileHoming = 1,
		AreaOfEffect = 2
	};

	class Weapon {
	public:
		std::string name;
		WeaponType type{ WeaponType::Projectile };
		uint32_t variant{ 0 };
		float speed{ 15.0f };
		float damage{ 25.0f };
		float cooldownTimer{ 0.0f };
		float cooldown{ 10.0f };
		void update(float delta);
	};
}