/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include "Player.hpp"

void Game::Entities::Player::addExperience(float value)
{
	// @todo: proper levelling
	experience += value;
}
