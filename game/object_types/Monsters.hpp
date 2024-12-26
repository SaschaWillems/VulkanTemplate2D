/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <string>

namespace Game {
	namespace ObjectTypes {

		class MonsterType {
		public:
			std::string name;
			std::string image;
			uint32_t imageIndex;
			float size;
			float health;
			float speed;
		};

		class MonsterTypeSet {
		public:
			std::vector<MonsterType> types{};
			std::string name;
		};

		class MonsterTypes {
		public:
			std::vector<MonsterTypeSet> sets{};
			void loadFromFile(const std::string jsonFileName);
		};

	}
}