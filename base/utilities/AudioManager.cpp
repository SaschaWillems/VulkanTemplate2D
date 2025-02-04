/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "AudioManager.h"

AudioManager* audioManager{ nullptr };

void AudioManager::addSoundFile(const std::string name, const std::string filename)
{
	auto soundBuffer = new sf::SoundBuffer;
	if (soundBuffer->loadFromFile(filename)) {
		soundBuffers[name] = soundBuffer;
	} else {
		std::cout << "Error: Could not load soundfile " << filename << "\n";
		delete soundBuffer;
	}
}

void AudioManager::playSnd(const std::string name)
{
	sound.setBuffer(*soundBuffers[name]);
	sound.setVolume(soundVolume);
	sound.play();
}

// @todo: cleanup
