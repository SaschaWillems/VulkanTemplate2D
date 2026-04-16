/*
* Copyright(C) 2024-2026 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include "Monster.hpp"

void Game::Entities::Monster::update(float delta)
{
	Entity::update(delta);
	for (auto& weapon : weapons) {
		weapon.update(delta);
	}
}