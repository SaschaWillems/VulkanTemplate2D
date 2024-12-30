/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <vector>
#include <random>
#include "time.h"
#include <SFML/Window.hpp>

#include "object_types/Monsters.hpp"
#include "entities/Entity.hpp"
#include "entities/Monster.hpp"
#include "entities/Player.hpp"
#include "entities/Projectile.hpp"
#include "entities/Pickup.hpp"
#include "entities/Number.hpp"

#include "AudioManager.h"

namespace Game {


	class Game {
	private:
		void monsterSpawnPosition(Entities::Monster& monster);
	public:
		std::default_random_engine randomEngine;

		ObjectTypes::MonsterTypes monsterTypes{};
		// @todo: Entity manager
		std::vector<Entities::Monster> monsters;
		std::vector<Entities::Projectile> projectiles;
		std::vector<Entities::Pickup> pickups;
		std::vector<Entities::Number> numbers;
		Entities::Player player;

		glm::vec2 playFieldSize;

		//
		float spawnTriggerTimer{ 0.0f };
		// Will be lowered with increasing game duration
		float spawnTriggerDuration{ 100.0f };
		// Will be increased with increasing game duration
		uint32_t spawnTriggerMonsterCount{ 16 };

		// @todo
		uint32_t projectileImageIndex;
		uint32_t experienceImageIndex;
		uint32_t firstNumberImageIndex;

		float playerFireTimer{ 0.0f };
		float playerFireTimerDuration{ 5.0f };

		Game();
		void spawnMonsters(uint32_t count);
		void spawnProjectile(Entities::Source source, uint32_t imageIndex, glm::vec2 position, glm::vec2 direction);
		void spawnPickup(Entities::Pickup pickup);
		void spawnNumber(uint32_t value, glm::vec2 position);

		void update(float delta);
		void updateInput(float delta);
	};
}