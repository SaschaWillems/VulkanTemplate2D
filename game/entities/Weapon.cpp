/*
* Copyright(C) 2024-2026 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include "Weapon.hpp"

void Game::Weapon::update(float delta)
{
	if (cooldownTimer < cooldown) {
		cooldownTimer += 25.0f * delta;
	}
}
