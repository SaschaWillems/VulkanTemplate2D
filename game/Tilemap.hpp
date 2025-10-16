/*
 * Copyright (C) 2023-2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <stdint.h>
#include "Texture.hpp"
#include "Sampler.hpp"
#include "DescriptorSet.hpp"

namespace Game {
	class Tilemap
	{
	public:
		uint32_t* data{ nullptr };
		vks::Texture2D* texture{ nullptr };
		Sampler* sampler{ nullptr };
		DescriptorSet* descriptorSetSampler{ nullptr };
		uint32_t imageIndex;
		uint32_t firstTileIndex;
		uint32_t lastTileIndex;
		uint32_t width{ 0 };
		uint32_t height{ 0 };
		~Tilemap();
		void setSize(uint32_t width, uint32_t height);
	};
}