/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include "Number.hpp"

void Game::Entities::Number::setValue(uint32_t value)
{
	this->value = value;
	// For easier access to digit count, size, etc.
	stringValue = std::to_string(value);
	digits = static_cast<uint32_t>(stringValue.length());
}
