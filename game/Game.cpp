/*
* Copyright(C) 2024-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include "Game.hpp"

void Game::Game::monsterSpawnPosition(Entities::Monster& monster)
{
	std::uniform_real_distribution<float> uniformDist(0.0, 1.0);
	glm::vec2 ring{ playFieldSize.x * 1.5f, playFieldSize.x * 1.75f };
	const float rho = sqrt((pow(ring[1], 2.0f) - pow(ring[0], 2.0f)) * uniformDist(randomEngine) + pow(ring[0], 2.0f));
	const float theta = static_cast<float>(2.0f * M_PI * uniformDist(randomEngine));
	monster.position = glm::vec2(rho * cos(theta), rho * sin(theta)) + player.position;
}

Game::Game::Game()
{
	randomEngine.seed((unsigned)time(nullptr));
	threadPool.setThreadCount(std::thread::hardware_concurrency());
}

void Game::Game::spawnMonsters(uint32_t count)
{
	// @todo: Not finished yet
	std::uniform_real_distribution<float> posDistX(-playFieldSize.x, playFieldSize.x);
	std::uniform_real_distribution<float> posDistY(-playFieldSize.y, playFieldSize.y);
	std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);
	std::uniform_real_distribution<float> speedDist(0.5f, 2.5f);
	std::uniform_real_distribution<float> scaleDist(0.5f, 1.0f);
	std::uniform_int_distribution<uint32_t> bossDist(0, 100);

	// Spawn in a ring centered at the player position
	for (auto i = 0; i < count; i++) {
		std::uniform_int_distribution<uint32_t> rndMonsterSet(0, static_cast<uint32_t>(monsterTypes.sets.size() - 1));
		const auto& monsterSet = monsterTypes.sets[rndMonsterSet(randomEngine)];
		std::uniform_int_distribution<uint32_t> rndMonster(0, static_cast<uint32_t>(monsterSet.types.size() - 1));
		const auto& monster = monsterSet.types[rndMonster(randomEngine)];

		Entities::Monster m;
		monsterSpawnPosition(m);
		m.imageIndex = monster.imageIndex;
		m.speed = speedDist(randomEngine);
		m.scale = scaleDist(randomEngine);

		// Randomly spawn a huge boss
		if (bossDist(randomEngine) >= 100 - spawnBossChance) {
			m.isBoss = true;
			// @todo: scale with level
			m.health = 250.0f;
			m.scale = scaleDist(randomEngine) * 2.5f;
		}
		
		// Replace dead monsters first
		for (auto& mon : monsters) {
			if (mon.state == Entities::State::Dead) {
				mon = m;
				continue;
			}
		}

		monsters.push_back(m);
	}
}

void Game::Game::spawnProjectile(Entities::Source source, uint32_t imageIndex, glm::vec2 position, glm::vec2 direction)
{
	// @todo: grow in chunks
	// @todo: Add projectile types with properties like speed, movement pattern, damage, source, tc.
	Entities::Projectile projectile{};
	projectile.position = position;
	projectile.direction = direction;
	projectile.imageIndex = imageIndex;
	projectile.source = source;
	projectile.damage = 25.0f;
	projectile.life = 100.0f;
	projectile.speed = 15.0f;
	projectile.scale = 0.5f;
	projectile.state = Entities::State::Alive;
	// Replace dead projectiles first
	for (auto& proj : projectiles) {
		if (proj.state == Entities::State::Dead) {
			proj = projectile;
			return;
		}
	}
	projectiles.push_back(projectile);
}

void Game::Game::spawnPickup(Entities::Pickup pickup)
{
	// Replace dead pickups first
	for (auto& pup : pickups) {
		if (pup.state == Entities::State::Dead) {
			pup = pickup;
			return;
		}
	}
	pickups.push_back(pickup);
}

void Game::Game::spawnNumber(uint32_t value, glm::vec2 position, Entities::Effect effect)
{
	// @todo: grow in chunks
	Entities::Number number{};
	number.position = position;
	number.direction = glm::vec2(0.f, -1.0f);
	number.life = 100.0f;
	number.setValue(value);
	if (effect != Entities::Effect::None) {
		number.setEffect(effect);
		number.scale *= 1.5f;
	}
	// Replace unused numbers first
	for (auto& num : numbers) {
		if (num.state == Entities::State::Dead) {
			num = number;
			return;
		}
	}
	numbers.push_back(number);
}

void Game::Game::monsterProjectileCollisionCheck(Entities::Monster& monster)
{
	std::uniform_real_distribution<float> critDist(0.0, 100.0f);
	for (auto& projectile : projectiles) {
		if (projectile.state == Entities::State::Dead) {
			continue;
		}
		if (projectile.source == Entities::Source::Player) {
			// @todo: Proper collision check
			if (abs(glm::distance(monster.position, projectile.position)) < monster.scale) {
				// @todo: Projectiles that can hit multiple enemies before "dying"
				projectile.state = Entities::State::Dead;
				// @todo: Move logic to entity
				float damage = projectile.damage;
				// @todo: Move elsewhere
				if (critDist(randomEngine) <= player.criticalChance) {
					damage *= player.criticalDamageMultiplier;
					projectile.effect = Entities::Effect::Critical;
				}
				monster.health -= damage;
				monster.setEffect(Entities::Effect::Hit);
				spawnNumber(damage, monster.position, projectile.effect);
				// @todo: Use instance color and timer to highlight hit monsters for a short duration
				if (monster.health <= 0.0f) {
					monster.state = Entities::State::Dead;
					// @todo
					Entities::Pickup xpPickup{};
					xpPickup.type = Entities::Pickup::Type::Experience;
					xpPickup.position = monster.position;
					xpPickup.value = 10;
					xpPickup.imageIndex = experienceImageIndex;
					xpPickup.scale = 0.5f;
					xpPickup.speed = player.speed * 2.0f;
					if (monster.isBoss) {
						xpPickup.value = 100;
						xpPickup.scale = 1.5f;
					}
					spawnPickup(xpPickup);
					audioManager->playSnd("enemydeath");
					currentRun.monstersKilled++;
				}
				else {
					audioManager->playSnd("enemyhit");
				}
			}
			//if (projectile.state == Entities::State::Dead) {
			//	break;
			//}
		}
	}
}

void Game::Game::update(float delta)
{
	// @todo: totally work in progress
	// @todo: use multi threading


	currentRun.update(delta);

	// Player projectiles
	playerFireTimer += delta * 25.0f;
	if (playerFireTimer > playerFireTimerDuration) {
		playerFireTimer = 0.0f;
		std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);
		spawnProjectile(Entities::Source::Player, projectileImageIndex, player.position, glm::vec2(dirDist(randomEngine), dirDist(randomEngine)));
		// @todo
		audioManager->playSnd("laser");
	}

	/*
	{
		ZoneScopedN("Collision detection");
		// Collision check
		for (auto& projectile : projectiles) {
			if (projectile.state == Entities::State::Dead) {
				continue;
			}
			// @todo: effect on hit target
			// @todo: move collision check to entity
			if (projectile.source == Entities::Source::Player) {
				//for (auto& monster : monsters) {
				//	if (monster.state == Entities::State::Dead) {
				//		continue;
				//	}
				//	// @todo: Proper collision check
				//	if (abs(glm::distance(monster.position, projectile.position)) < 1.0f) {
				//		// @todo: Projectiles that can hit multiple enemies before "dying"
				//		projectile.state = Entities::State::Dead;
				//		// @todo: Move logic to entity
				//		monster.health -= projectile.damage;
				//		monster.setEffect(Entities::Effect::Hit);
				//		spawnNumber(projectile.damage, monster.position);
				//		// @todo: Use instance color and timer to highlight hit monsters for a short duration
				//		if (monster.health <= 0.0f) {
				//			monster.state = Entities::State::Dead;
				//			// @todo
				//			Entities::Pickup xpPickup{};
				//			xpPickup.type = Entities::Pickup::Type::Experience;
				//			xpPickup.position = monster.position;
				//			xpPickup.value = 10;
				//			xpPickup.imageIndex = experienceImageIndex;
				//			xpPickup.scale = 0.5f;
				//			xpPickup.speed = player.speed * 2.0f;
				//			spawnPickup(xpPickup);
				//			audioManager->playSnd("enemydeath");
				//		}
				//		else {
				//			audioManager->playSnd("enemyhit");
				//		}
				//	}
				//	if (projectile.state == Entities::State::Dead) {
				//		break;
				//	}
				//}
			}
		}
	}
	*/

	{
		ZoneScopedN("Entity updates");

		threadPool.threads[0]->addJob([=] {
			for (auto& pickup : pickups) {
				if (pickup.state == Entities::State::Dead) {
					continue;
				}
				// Experience moves towards the player once he gets in pickup range
				// @todo: Pickup range as property of player
				if (pickup.type == Entities::Pickup::Type::Experience) {
					if (glm::distance(pickup.position, player.position) < player.pickupDistance) {
						pickup.direction = glm::normalize(player.position - pickup.position);
						// @todo: pickup speed adjustments
						pickup.position += pickup.direction * pickup.speed * delta;
						// @todo: Proper collision check
						if (glm::distance(player.position, pickup.position) < 1.0f) {
							pickup.state = Entities::State::Dead;
							player.addExperience(pickup.value);
							audioManager->playSnd("pickupxp");
							// @todo: move to somewhere else
							if (player.experience >= getNextLevelExp(player.level + 1)) {
								player.level++;
								// @todo: rethink
								setState(GameState::LevelUp);
							}
						}
					}
				}
			}
		});

		threadPool.threads[1]->addJob([=] { 
			for (auto i = 0; i < projectiles.size(); i++) {
				Entities::Projectile& projectile = projectiles[i];
				projectile.position += projectile.direction * projectile.speed * delta;
				projectile.life -= delta * 50.0f;
				if (projectile.life <= 0.0f) {
					projectile.state = Entities::State::Dead;
				}
			}
		});

		threadPool.threads[2]->addJob([=] {
			for (auto i = 0; i < numbers.size(); i++) {
				Entities::Number& number = numbers[i];
				number.position += number.direction * number.speed * delta;
				number.life -= delta * 50.0f;
				if (number.life <= 0.0f) {
					number.state = Entities::State::Dead;
				}
			}
		});

		// @todo: set min. no of monsters per thread for lower monster counts

		const int32_t maxHardwareThreads = static_cast<int32_t>(std::thread::hardware_concurrency());
		const auto maxMonsterThreads = std::max(maxHardwareThreads - 3, 1);

		const auto mtSize = monsters.size() / maxMonsterThreads;
		for (auto t = 0; t < maxMonsterThreads; t++) {
			threadPool.threads[3 + t]->addJob([=] {
				const auto start = t * mtSize;
				auto end = start + mtSize;
				if (end > monsters.size()) {
					end = monsters.size();
				}
				for (auto i = start; i < end; i++) {
					Entities::Monster& monster = monsters[i];
					if (monster.state == Entities::State::Dead) {
						continue;
					}
					// @todo: simple "logic" for testing
					// @todo: Use velocity
					// Monsters far away respawn outside of the view
					monster.update(delta);
					if (glm::length(player.position - monster.position) > playFieldSize.x * 3.0f) {
						monsterSpawnPosition(monster);
					};
					monster.direction = glm::normalize(player.position - monster.position);
					monster.position += monster.direction * monster.speed * delta;
					monsterProjectileCollisionCheck(monster);
				}
			});
		}

		threadPool.wait();
	}

	// Monster spawn
	spawnTriggerTimer += delta * 25.0f;
	if (spawnTriggerTimer > spawnTriggerDuration) {
		spawnTriggerTimer = 0.0f;
		spawnMonsters(spawnTriggerMonsterCount);
	}
}

void Game::Game::updateInput(float delta)
{
	float playerSpeed = player.speed;
	if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift)) {
		if (player.stamina > 0.0f) {
			playerSpeed *= 2.0f;
		}
		player.stamina -= delta * 2.5f;
		if (player.stamina < 0.0f) {
			player.stamina = 0.0f;
		}
	} else {
		if (player.stamina < player.maxStamina) {
			player.stamina += delta * 1.5f;
			if (player.stamina > player.maxStamina) {
				player.stamina = player.maxStamina;
			};
		}
	}
	glm::vec2 playerTilePos = tilemap.tilePosFromVisualPos(player.position);
	if (sf::Keyboard::isKeyPressed(sf::Keyboard::A)) {
		player.position.x -= playerSpeed * delta;
		if (playerTilePos.x < 0.0f) {
			player.position.x = 0.0f;
		}
	}
	if (sf::Keyboard::isKeyPressed(sf::Keyboard::D)) {
		player.position.x += playerSpeed * delta;
		if (player.position.x > (tilemap.width - 1) / tilemap.screenFactor.x) {
			player.position.x = (tilemap.width - 1) / tilemap.screenFactor.x;
		}
	}
	if (sf::Keyboard::isKeyPressed(sf::Keyboard::W)) {
		player.position.y -= playerSpeed * delta;
		if (playerTilePos.y < 0.0f) {
			player.position.y = 0.0f;
		}
	}
	if (sf::Keyboard::isKeyPressed(sf::Keyboard::S)) {
		player.position.y += playerSpeed * delta;
		if (player.position.y > (tilemap.height - 1) / tilemap.screenFactor.y) {
			// @todo
			player.position.y = (tilemap.height - 1) / tilemap.screenFactor.y;
		}
	}
}

int32_t Game::Game::getNextLevelExp(int32_t level)
{
	// @todo: need to adjust this
	float exponent = 1.25f;
	float base = 500;
	return floor(base * (pow(level, exponent)));
}
