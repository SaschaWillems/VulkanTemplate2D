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
#include <tracy/Tracy.hpp>
#include <Threadpool.hpp>

#include "Run.hpp"
#include "object_types/Monsters.hpp"
#include "entities/Entity.hpp"
#include "entities/Monster.hpp"
#include "entities/Player.hpp"
#include "entities/Projectile.hpp"
#include "entities/Pickup.hpp"
#include "entities/Number.hpp"
#include "Tilemap.hpp"

#include "AudioManager.h"

namespace Game {


	class Game {
	private:
		vks::ThreadPool threadPool;
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
		Tilemap tilemap;

		glm::vec2 playFieldSize;

		// @todo: Move these to a global header file
		float spawnTriggerTimer{ 0.0f };
		// Will be lowered with increasing game duration
		float spawnTriggerDuration{ 100.0f };
		// Will be increased with increasing game duration
		uint32_t spawnTriggerMonsterCount{ 16 };
		// Chance that an enemy spawns as a boss (in percent) 
		int spawnBossChance{ 1 };

		// @todo
		uint32_t projectileImageIndex;
		uint32_t experienceImageIndex;
		uint32_t firstNumberImageIndex;

		float playerFireTimer{ 0.0f };
		float playerFireTimerDuration{ 5.0f };

		Run currentRun;

		Game();
		void spawnMonsters(uint32_t count);
		void spawnProjectile(Entities::Source source, uint32_t imageIndex, glm::vec2 position, glm::vec2 direction);
		void spawnPickup(Entities::Pickup pickup);
		void spawnNumber(uint32_t value, glm::vec2 position, Entities::Effect effect = Entities::Effect::None);

		void monsterProjectileCollisionCheck(Entities::Monster& monster);
		void update(float delta);
		void updateInput(float delta);
		int32_t getNextLevelExp(int32_t level);
	};
}