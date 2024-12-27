/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string>
#include <iostream>
#include <unordered_map>
#include <SFML/Audio.hpp>

// @todo: std::(de)queue?

#undef PlaySoundA

class AudioManager {
private:
	sf::Sound sound;
public:
	uint32_t soundVolume{ 50 };
	uint32_t musicVolume{ 100 };
	std::unordered_map<std::string, sf::SoundBuffer*> soundBuffers;
	void addSoundFile(const std::string name, const std::string filename);
	// Named like this to avoid a WinApi macro (PlaySoundA)
	void playSnd(const std::string name);
};

extern AudioManager* audioManager;