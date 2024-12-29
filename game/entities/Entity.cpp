/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include "Entity.hpp"

void Game::Entities::Entity::setEffect(Effect effect)
{
	// @todo: maybe make virtual and move to player, monster, etc.
	this->effect = effect;
	if (effect == Effect::Hit) {
		effectTimer = 0.25f;
	}
}

void Game::Entities::Entity::update(float delta)
{
	if (effectTimer > 0.0f) {
		effectTimer -= delta;
		if (effectTimer <= 0.0f) {
			effect = Effect::None;
		}
	}
}