/*
* Copyright(C) 2024-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "Entity.hpp"

namespace Game {
	namespace Entities {
		class Monster : public Entity {
		public:
			bool isBoss{ false };
		};
	}
}