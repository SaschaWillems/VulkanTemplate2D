/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "Entity.hpp"

namespace Game {
	namespace Entities {
		class Pickup : public Entity {
		public:
			enum Type {
				Experience = 0,
				Currency = 1,
				Health = 2,
				Powerup = 3,
			};

			Type type;
			float value;
		};
	}
}