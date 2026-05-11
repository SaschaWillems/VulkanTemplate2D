/*
 * Copyright (C) 2023-2026 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "Tilemap.hpp"

Game::Tilemap::~Tilemap()
{
	//if (data) {
	//	delete[] data;
	//}
}

void Game::Tilemap::setSize(uint32_t width, uint32_t height)
{
	//if (data) {
	//	delete[] data;
	//}
	//data = new uint32_t[width * height];
	//this->width = width;
	//this->height = height;
}

void Game::Tilemap::save(const std::string filename)
{
	std::fstream file;
	file.open(filename, std::ios::trunc | std::ios::binary | std::fstream::out);
	assert(file.is_open());
	// @todo: proper header (incl. tileset name and version)
	struct Header {
		uint32_t width;
		uint32_t height;
	};
	Header header{
		.width = TILEMAP_MAX_DIM,
		.height = TILEMAP_MAX_DIM
	};
	file.write((char*)&header, sizeof(Header));
	file.write((char*)&backgroundLayer, TILEMAP_MAX_DIM * TILEMAP_MAX_DIM * sizeof(uint32_t));
	file.write((char*)&foregroundLayer, TILEMAP_MAX_DIM* TILEMAP_MAX_DIM * sizeof(uint32_t));
	file.close();
}

void Game::Tilemap::load(const std::string filename)
{
	std::fstream file;
	file.open(filename, std::ios::binary | std::fstream::in);
	assert(file.is_open());
	struct Header {
		uint32_t width;
		uint32_t height;
	} header{};
	file.read((char*)&header, sizeof(Header));
	file.read((char*)&backgroundLayer, TILEMAP_MAX_DIM * TILEMAP_MAX_DIM * sizeof(uint32_t));
	file.read((char*)&foregroundLayer, TILEMAP_MAX_DIM * TILEMAP_MAX_DIM * sizeof(uint32_t));
	file.close();
}