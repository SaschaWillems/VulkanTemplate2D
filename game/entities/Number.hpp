/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "Entity.hpp"
#include <string>

namespace Game {
	namespace Entities {
		class Number : public Entity {
		private:
			uint32_t value;
		public:
			std::string stringValue;
			float life;
			glm::vec3 color{ 1.0f };
			uint32_t digits;
			void setValue(uint32_t value);
		};
	}
}