/*
* Copyright(C) 2024-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include <vector>
#include <chrono>

namespace Game {
	class Run {
	public:
		int32_t monstersKilled{ 0 };
		float duration{ 0 };
		void update(float delta);
	};
}