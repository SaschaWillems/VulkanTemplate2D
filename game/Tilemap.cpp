/*
 * Copyright (C) 2023-2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "Tilemap.hpp"

Game::Tilemap::~Tilemap()
{
	if (data) {
		delete[] data;
	}
}

void Game::Tilemap::setSize(uint32_t width, uint32_t height)
{
	if (data) {
		delete[] data;
	}
	data = new uint32_t[width * height];
	this->width = width;
	this->height = height;
}

glm::ivec2 Game::Tilemap::tilePosFromVisualPos(glm::vec2 visualPos) const {
	return glm::ivec2{ (int)(floor(visualPos.x * screenFactor.x )), (int)(floor(visualPos.y * screenFactor.y )) };
}
