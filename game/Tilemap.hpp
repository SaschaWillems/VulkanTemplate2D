/*
 * Copyright (C) 2023-2026 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <stdint.h>
#include <iostream>
#include <fstream>
#include "Texture.hpp"
#include "Sampler.hpp"
#include "DescriptorSet.hpp"
#include <glm/glm.hpp>

// @todo: multiple layers (background/foreground)

constexpr uint32_t TILEMAP_MAX_DIM = 64; // @todo: 2k or 4k

namespace Game {
	typedef uint32_t TilemapLayer[TILEMAP_MAX_DIM][TILEMAP_MAX_DIM];
	class Tilemap
	{
	public:
		// @todo: multiple layers? (background/foreground)
		TilemapLayer backgroundLayer;
		TilemapLayer foregroundLayer;
		uint32_t firstTileIndex;
		uint32_t lastTileIndex;
		uint32_t width{ TILEMAP_MAX_DIM };
		uint32_t height{ TILEMAP_MAX_DIM };
		// Used to calculate actual tile index from visual screen position
		glm::vec2 screenFactor{ 0.0f };
		~Tilemap();
		void setSize(uint32_t width, uint32_t height);
		void save(const std::string filename);
		void load(const std::string filename);
	};
}