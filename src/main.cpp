/*
 * Copyright (C) 2023-2026 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "VulkanContext.h"
#include "FileWatcher.hpp"
#include <filesystem>
#include <VulkanApplication.h>
#include "AudioManager.h"
#include "Texture.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <random>
#include <format>
#include "time.h"
#include <SFML/Audio.hpp>
#include <json.hpp>
#include "Game.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// @todo: audio (music and sfx)
// @todo: sync2 everywhere
// @todo: timeline semaphores

#define USE_REBAR

#ifdef TRACY_ENABLE
void* operator new(size_t count)
{
	auto ptr = malloc(count);
	TracyAlloc(ptr, count);
	return ptr;
}

void operator delete(void* ptr) noexcept
{
	TracyFree(ptr);
	free(ptr);
}
#endif

std::vector<Pipeline*> pipelineList{};

struct ShaderData {
	glm::mat4 mvp;
	float time{ 0.0f };
	float timer{ 0.0f };
	float viewportAR;
	float postProcessTimer{ 0.0f };
	glm::vec2 screenRes{ 0.0f };
	uint32_t lightCount{ 0 };
	float dayNightCycle{ 0.0f };
} shaderData;

struct Vertex {
	float pos[3];
	float uv[2];
};

struct IV2 {
	uint32_t x;
	uint32_t y;
};

struct InstanceData {
	glm::vec3 pos;
	float scale{ 1.0f };
	uint32_t imageIndex{ 0 };
	uint32_t effect{ 0 };
};

struct TilemapInstanceData {
	IV2 pos;
	// @todo: smaller data type
	uint32_t imageIndex{ 0 };
	uint32_t effect{ 0 };
};

struct LightSource {
	alignas(16) glm::vec2 pos{ 0.0f };
	alignas(16) glm::vec3 color{ 1.0f };
	float radius;
};

enum class PostProcessEffect {
	None = 0,
	FadeIn = 1
};

float kbDebounce;

// AngelCode .fnt format structs and classes

struct bmchar {
	uint32_t x, y;
	uint32_t width;
	uint32_t height;
	int32_t xoffset;
	int32_t yoffset;
	int32_t xadvance;
	uint32_t page;
};
std::array<bmchar, 255> fontChars;

Game::Game game;

// @todo
struct UIText {
	glm::vec2 pos;
	//glm::vec3 color;
	std::string text;
};

struct UI {
	std::vector<UIText> textElements;
};
UI ui;

struct Editor {
	bool active{ false };
	glm::vec2 pos{ 0.0f };
	glm::ivec2 selectedTile{ 0 };
	uint32_t tileIndex{ 0 };
	byte activeLayer{ 0 };
	bool foregroundVisible{ true };
} editor;

class Application : public VulkanApplication {
private:
	// Changing buffers (e.g. instance, will increase by this size)
	const uint32_t instanceBufferBlockSizeIncrease{ 2048 };
	struct FrameObjects : public VulkanFrameObjects {
		Buffer* uniformBuffer{ nullptr };
		DescriptorSet* descriptorSet{ nullptr };

		Buffer* instanceBuffer{ nullptr };
		uint32_t instanceBufferSize{ 0 };
		uint32_t instanceBufferDrawCount{ 0 };
		uint32_t instanceBufferMaxCount{ 0 };
		InstanceData* instances{nullptr};

		LightSource* lights{ nullptr };
		uint32_t lightsBufferSize{ 0 };
		uint32_t lightsBufferDrawCount{ 0 };
		uint32_t lightsBufferMaxCount{ 0 };
		Buffer* lightsBuffer{ nullptr };
		DescriptorSet* descriptorSetLights{ nullptr };

		Buffer* uiBuffer{ nullptr };
		uint32_t uiBufferSize{ 0 };
		uint32_t uiBufferVertexCount{ 0 };

		Buffer* uiTextBuffer{ nullptr };
		uint32_t uiTextBufferSize{ 0 };
		uint32_t uiTextBufferVertexCount{ 0 };

		// @todo: Tilemap rendering
		uint32_t tilemapInstanceCount{ 0 };
		Buffer* tilemapInstanceBuffer{ nullptr };
		uint32_t tilemapForegroundInstanceCount{ 0 };
		// @todo: Separate projectiles into own set of instance buffers (due to different update frequency?)
		//struct Projectiles {
		//	Buffer* instanceBuffer{ nullptr };
		//	uint32_t instanceBufferSize{ 0 };
		//	uint32_t instanceBufferDrawCount{ 0 };
		//} projectiles;
	};
	TilemapInstanceData* tilemapInstances{ nullptr };
	// One large staging buffer that's reused for all copies
	// @todo: per frame?
	const size_t stagingBufferSize = 64 * 1024 * 1024;
	Buffer* stagingBuffer{ nullptr };
	CommandBuffer* copyCommandBuffer{ nullptr };

	// One set for all images
	std::vector<VkDescriptorImageInfo> textureDescriptors{};
	std::vector<VkDescriptorImageInfo> samplerDescriptors{};
	std::vector<vks::Texture2D*> textures{};
	Sampler* spriteSampler{ nullptr };
	Sampler* renderImageSampler{ nullptr };

	std::vector<FrameObjects> frameObjects;
	FileWatcher* fileWatcher{ nullptr };
	DescriptorPool* descriptorPool{ nullptr };
	DescriptorSetLayout* descriptorSetLayoutUniforms{ nullptr };
	DescriptorSetLayout* descriptorSetLayoutSamplers{ nullptr };
	DescriptorSetLayout* descriptorSetLayoutTextures{ nullptr };
	DescriptorSetLayout* descriptorSetLayoutLights{ nullptr };
	DescriptorSetLayout* descriptorSetLayoutRenderImage{ nullptr };
	DescriptorSet* descriptorSetTextures{ nullptr };
	DescriptorSet* descriptorSetSamplers{ nullptr };
	DescriptorSet* descriptorSetRenderImage{ nullptr };
	std::unordered_map<std::string, PipelineLayout*> pipelineLayouts;
	std::unordered_map<std::string, Pipeline*> pipelines;
	sf::Music backgroundMusic;
	Buffer* quadBuffer{ nullptr };
	const glm::vec2 screenDimBase{ 12.5f };
	glm::vec2 screenDim{ screenDimBase };
	PostProcessEffect postProcessEffect{ PostProcessEffect::None };
	float postProcessTimeFactor{ 1.0f };
	uint32_t visibleTileCount{ 32 };
	uint32_t crtFrameImageIndex{ 0 };
	uint32_t fontImageIndex{ 0 };
public:	
	Application() : VulkanApplication() {
		apiVersion = VK_API_VERSION_1_3;

		Device::enabledFeatures.shaderClipDistance = VK_TRUE;
		Device::enabledFeatures.samplerAnisotropy = VK_TRUE;
		Device::enabledFeatures.depthClamp = VK_TRUE;
		Device::enabledFeatures.fillModeNonSolid = VK_TRUE;

		Device::enabledFeatures11.multiview = VK_TRUE;
		Device::enabledFeatures11.shaderDrawParameters = VK_TRUE;
		Device::enabledFeatures12.descriptorIndexing = VK_TRUE;
		Device::enabledFeatures12.runtimeDescriptorArray = VK_TRUE;
		Device::enabledFeatures12.descriptorBindingVariableDescriptorCount = VK_TRUE;
		Device::enabledFeatures13.dynamicRendering = VK_TRUE;

		//settings.sampleCount = VK_SAMPLE_COUNT_4_BIT;

		audioManager = new AudioManager();

		slangCompiler = new SlangCompiler();

		title = "Bindless Survivors";
	}
		

	~Application() {		
		vkDeviceWaitIdle(VulkanContext::device->logicalDevice);
		for (FrameObjects& frame : frameObjects) {
			destroyBaseFrameObjects(frame);
			delete frame.uniformBuffer;
			delete frame.instanceBuffer;
			delete frame.lightsBuffer;
			delete frame.uiBuffer;
			delete frame.uiTextBuffer;
			delete[] frame.instances;
			delete frame.tilemapInstanceBuffer;
		}
		delete stagingBuffer;
		if (fileWatcher) {
			fileWatcher->stop();
			delete fileWatcher;
		}
		for (auto& it : pipelines) {
			delete it.second;
		}
		for (auto& texture : textures) {
			delete texture;
		}
		//for (auto& texture : tileMap.textures) {
		//	delete texture;
		//}
		//delete tileMap.texture;
		if (!copyCommandBuffer) {
			delete copyCommandBuffer;
		}
		delete descriptorPool;
		delete descriptorSetLayoutUniforms;

		// @todo: move to manager class
		if (backgroundMusic.Playing) {
			backgroundMusic.stop();
		}
		delete audioManager;
		delete quadBuffer;

		delete slangCompiler;
	}

	void setPostProcessEffect(PostProcessEffect effect) {
		postProcessEffect = effect;
		switch (effect) {
		case PostProcessEffect::FadeIn:
			shaderData.postProcessTimer = 0.0f;
			postProcessTimeFactor = 0.25f;
		}
	}

	void updatePostProcessEffect(float delta)
	{
		shaderData.postProcessTimer += delta * postProcessTimeFactor;
		switch (postProcessEffect) {
		case PostProcessEffect::FadeIn:
			if (shaderData.postProcessTimer >= 1.0f) {
				setPostProcessEffect(PostProcessEffect::None);
			}
		}
	}

	void loadTexture(const std::string filename, uint32_t& index)
	{
		int width, height, channels;
		unsigned char* img = stbi_load(filename.c_str(), &width, &height, &channels, 4);
		size_t imgSize = static_cast<uint32_t>(width * height * 4);
		assert(img != nullptr);

		vks::TextureFromBufferCreateInfo texCI = {
			.buffer = img,
			.bufferSize = imgSize,
			.texWidth = static_cast<uint32_t>(width),
			.texHeight = static_cast<uint32_t>(height),
			.format = VK_FORMAT_R8G8B8A8_SRGB,
			.createSampler = false,
		};
		vks::Texture2D* tex = new vks::Texture2D(texCI);
		textures.push_back(tex);

		stbi_image_free(img);

		index = static_cast<uint32_t>(textures.size() - 1);
	}

	// Basic parser for AngelCode bitmap font format files
	void loadBmFont()
	{
		auto nextValuePair = [](std::stringstream* stream) {
			std::string pair;
			*stream >> pair;
			size_t spos = pair.find("=");
			std::string value = pair.substr(spos + 1);
			int32_t val = std::stoi(value);
			return val;
		};

		std::string fileName = getAssetPath() + "default-font.fnt";
		std::filebuf fileBuffer;
		fileBuffer.open(fileName, std::ios::in);
		std::istream istream(&fileBuffer);
		assert(istream.good());
		while (!istream.eof()) {
			std::string line;
			std::stringstream lineStream;
			std::getline(istream, line);
			lineStream << line;
			std::string info;
			lineStream >> info;
			if (info == "char") {
				uint32_t charid = nextValuePair(&lineStream);
				if (charid > 255) {
					continue;
				}
				fontChars[charid].x = nextValuePair(&lineStream);
				fontChars[charid].y = nextValuePair(&lineStream);
				fontChars[charid].width = nextValuePair(&lineStream);
				fontChars[charid].height = nextValuePair(&lineStream);
				fontChars[charid].xoffset = nextValuePair(&lineStream);
				fontChars[charid].yoffset = nextValuePair(&lineStream);
				fontChars[charid].xadvance = nextValuePair(&lineStream);
				fontChars[charid].page = nextValuePair(&lineStream);
			}
		}
	}

	void loadTexture(const std::string filename)
	{
		uint32_t index;
		loadTexture(filename, index);
	}

	void loadAssets() {		
		game.monsterTypes.loadFromFile(getAssetPath() + "game/monsters.json");
		// @todo
		const std::string tileSet{ "set0" };
		for (auto& set : game.monsterTypes.sets) {
			for (auto& type : set.types) {
				loadTexture(getAssetPath() + "game/monsters/" + type.image, type.imageIndex);
			}
		}

		// Font
		loadBmFont();
		loadTexture(getAssetPath() + "default-font.png", fontImageIndex);

		// Numbers
		game.firstNumberImageIndex = static_cast<uint32_t>(textures.size());
		for (uint32_t i = 0; i < 10; i++) {
			loadTexture(getAssetPath() + "game/numbers/num_" + std::to_string(i) + ".png");
		}

		// @todo: Player images
		loadTexture(getAssetPath() + "game/players/human_male.png", game.player.imageIndex);

		// @todo: Projectile images
		loadTexture(getAssetPath() + "game/projectiles/magic_bolt_1.png", game.projectileImageIndex);
		loadTexture(getAssetPath() + "game/projectiles/magic_bolt_4.png", game.projectileImageIndexMonster);
		loadTexture(getAssetPath() + "game/pickups/misc_crystal_old.png", game.experienceImageIndex);

		// Tile map
		game.tilemap.firstTileIndex = static_cast<uint32_t>(textures.size());
		for (const auto& file : std::filesystem::directory_iterator(getAssetPath() + "game/tiles/" + tileSet)) {
			if (file.path().extension() == ".png") {
				loadTexture(file.path().string());
			}
		}
		game.tilemap.lastTileIndex = static_cast<uint32_t>(textures.size());
		
		loadTexture(getAssetPath() + "game/crtframe.png", crtFrameImageIndex);

		// Game UI
		loadTexture(getAssetPath() + "game/ui.png", game.uiImageIndex);
		SamplerCreateInfo samplerCI {
			.name = "Sprite sampler",
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		};
		spriteSampler = new Sampler(samplerCI);

		// @todo
		// Audio
		const std::map<std::string, std::string> soundFiles = {
			{ "laser", "sounds/sfx_wpn_laser7.wav" },
			{ "enemyhit", "sounds/sfx_exp_various1.wav" },
			{ "enemydeath", "sounds/sfx_exp_medium1.wav" },
			{ "pickupxp", "sounds/sfx_coin_double4.wav" }
		};

		for (auto& it : soundFiles) {
			audioManager->addSoundFile(it.first, getAssetPath() + it.second);
		}
	}

	void initTileMap()
	{
		auto& tilemap = game.tilemap;

		// @todo: random tiles for testing
		std::uniform_int_distribution<uint32_t> rndTile(0, static_cast<uint32_t>(0, 2));
		for (auto x = 0; x < TILEMAP_MAX_DIM; x++) {
			for (auto y = 0; y < TILEMAP_MAX_DIM; y++) {
				// Border
				if (y == 0 || y == TILEMAP_MAX_DIM - 1 || x == 0 || x == TILEMAP_MAX_DIM - 1) {
					tilemap.backgroundLayer[x][y] = 0;
					continue;
				}
				tilemap.backgroundLayer[x][y] = 1;
				tilemap.foregroundLayer[x][y] = -1;
			}
		}

	}

	void updateTextureDescriptor() {
		// @todo: actual update logic

		// Use one large descriptor set for all imgages
		textureDescriptors.clear();
		for (auto& tex : textures) {
			textureDescriptors.push_back(tex->descriptor);
		}

		uint32_t textureCount = static_cast<uint32_t>(textureDescriptors.size());
		descriptorSetLayoutTextures = new DescriptorSetLayout({
			.descriptorIndexing = true,
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = textureCount, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}
			}
		});

		descriptorSetTextures = new DescriptorSet({
			.pool = descriptorPool,
			.variableDescriptorCount = textureCount,
			.layouts = { descriptorSetLayoutTextures->handle },
			.descriptors = {
				{.dstBinding = 0, .descriptorCount = textureCount, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .pImageInfo = textureDescriptors.data()}
			}
		});

		// Samplers
		// @todo: only one sampler right mow
		samplerDescriptors.clear();
		samplerDescriptors.push_back(spriteSampler->descriptor);

		const uint32_t samplerCount = static_cast<uint32_t>(samplerDescriptors.size());
		descriptorSetLayoutSamplers = new DescriptorSetLayout({
			.descriptorIndexing = true,
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = samplerCount, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
			}
		});

		descriptorSetSamplers = new DescriptorSet({
			.pool = descriptorPool,
			.variableDescriptorCount = samplerCount,
			.layouts = { descriptorSetLayoutSamplers->handle },
			.descriptors = {
				{.dstBinding = 0, .descriptorCount = samplerCount, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = samplerDescriptors.data()},
			}
		});
	}

	void generateQuad()
	{
		std::vector<Vertex> vertices =
		{
			{ {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f } },
			{ { -0.5f,  0.5f, 0.0f }, { 0.0f, 1.0f } },
			{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f } },

			{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f } },
			{ {  0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f } },
			{ {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f } },
		};

		const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);

		// Stage to device
		Buffer* stagingBuffer = new Buffer({
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.size = vertexBufferSize,
			.data = vertices.data()
		});

		quadBuffer = new Buffer({
			.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.size = vertexBufferSize,
		});

		CommandBuffer* cb = new CommandBuffer({
			.device = *vulkanDevice,
			.pool = commandPool
		});

		cb->begin();
		VkBufferCopy bufferCopy = { .size = vertexBufferSize };
		vkCmdCopyBuffer(cb->handle, stagingBuffer->buffer, quadBuffer->buffer, 1, &bufferCopy);
		cb->end();
		cb->oneTimeSubmit(queue);
		delete cb;		
		
		delete stagingBuffer;
	}

	// Tilemap is rendered via instancing, one per each visible tile
	void updateTileMap(FrameObjects& frame) {
		Game::Tilemap& tilemap = game.tilemap;

		const size_t maxInstanceBufferDim = TILEMAP_MAX_DIM * TILEMAP_MAX_DIM + TILEMAP_MAX_DIM * TILEMAP_MAX_DIM;

		if (!tilemapInstances) {
			// @todo: no need to be that big...
			tilemapInstances = new TilemapInstanceData[maxInstanceBufferDim];
		}

		if (!frame.tilemapInstanceBuffer) {
			frame.tilemapInstanceBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				.size = maxInstanceBufferDim * sizeof(TilemapInstanceData),
				.vmaAllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.map = true,
			});
		}

		frame.tilemapInstanceCount = 0;
		glm::ivec2 currentTilePos = editor.active ? glm::ivec2(editor.pos) : game.player.tilePos();
		int32_t sx = currentTilePos.x - (int32_t)(screenDim.x * 1.25f);
		int32_t ex = currentTilePos.x + (int32_t)(screenDim.x * 1.25f);
		int32_t sy = currentTilePos.y - (int32_t)(screenDim.y * 1.25f);
		int32_t ey = currentTilePos.y + (int32_t)(screenDim.y * 1.25f);
		// Background
		for (int32_t y = sy; y <= ey; y++) {
			for (int32_t x = sx; x <= ex; x++) {
				if ((x < 0) || (y < 0) || (x > TILEMAP_MAX_DIM - 1) || (y > TILEMAP_MAX_DIM - 1)) {
					continue;
				}
				tilemapInstances[frame.tilemapInstanceCount] = {
					.pos = {.x = (uint32_t)x, .y = (uint32_t)y },
					.imageIndex = tilemap.backgroundLayer[x][y] + game.tilemap.firstTileIndex
				};
				frame.tilemapInstanceCount++;
			}
		}

		// In editor mode we want to show tiles that can be selected
		if (editor.active) {
			for (int32_t i = -3; i <= 3; i++) {
				int32_t sidx = (int32_t)editor.tileIndex + i;
				if (sidx < 0 || sidx >(game.tilemap.lastTileIndex - game.tilemap.firstTileIndex)) {
					continue;
				}
				tilemapInstances[frame.tilemapInstanceCount] = {
					.pos = {.x = ((uint32_t)sx + (ex - sx) / 2) + i, .y = currentTilePos.y + (uint32_t)(screenDim.y * 0.85)},
					.imageIndex = (uint32_t)i + game.tilemap.firstTileIndex + editor.tileIndex,
					.effect = (uint32_t)((i == 0) ? 1 : 2)
				};
				frame.tilemapInstanceCount++;
			}
		}

		// Foreground
		frame.tilemapForegroundInstanceCount = 0;
		if (!editor.active || (editor.active && editor.foregroundVisible)) {
			for (int32_t y = sy; y <= ey; y++) {
				for (int32_t x = sx; x <= ex; x++) {
					if ((x < 0) || (y < 0) || (x > TILEMAP_MAX_DIM - 1) || (y > TILEMAP_MAX_DIM - 1)) {
						continue;
					}
					if (tilemap.foregroundLayer[x][y] == -1) {
						continue;
					}
					tilemapInstances[frame.tilemapInstanceCount + frame.tilemapForegroundInstanceCount] = {
						.pos = {.x = (uint32_t)x, .y = (uint32_t)y },
						.imageIndex = tilemap.foregroundLayer[x][y] + game.tilemap.firstTileIndex
					};
					frame.tilemapForegroundInstanceCount++;
				}
			}
		}

#if defined(USE_REBAR)
		memcpy(frame.tilemapInstanceBuffer->mapped, &tilemapInstances[0], (frame.tilemapInstanceCount + frame.tilemapForegroundInstanceCount ) * sizeof(TilemapInstanceData));
#endif
	}

	void updateInstanceBuffer(FrameObjects& frame) {
		const uint32_t maxInstanceCount = 
			static_cast<uint32_t>(game.monsters.size()) +
			static_cast<uint32_t>(game.projectiles.size()) +
			static_cast<uint32_t>(game.pickups.size()) +
			// @todo: Max. 3 digits per number for now
			(static_cast<uint32_t>(game.numbers.size()) * 3) +
			1;

		// Only recreate buffer if necessary, resizing is done in "chunks" to avoid frequent resizes
		const int32_t minInstanceBufferCount = std::max(maxInstanceCount + instanceBufferBlockSizeIncrease - 1 - (maxInstanceCount + instanceBufferBlockSizeIncrease - 1) % instanceBufferBlockSizeIncrease, instanceBufferBlockSizeIncrease);
		if (frame.instanceBufferMaxCount < minInstanceBufferCount) {
			std::cout << "Resizing instance buffer for frame " << frame.index << " to " << minInstanceBufferCount << " elements\n";
			// Host
			delete[] frame.instances;
			frame.instances = new InstanceData[minInstanceBufferCount];
			// Device
			delete frame.instanceBuffer;
			frame.instanceBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				.size = minInstanceBufferCount * sizeof(InstanceData),
#if defined(USE_REBAR)
				.vmaAllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.map = true,
#endif
			});
#if defined(USE_REBAR)
			VkMemoryPropertyFlags memPropFlags;
			vmaGetAllocationMemoryProperties(VulkanContext::vmaAllocator, frame.instanceBuffer->bufferAllocation, &memPropFlags);
			// @todo: fall back to staging if no ReBAR
			assert(memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
#endif
			frame.instanceBufferMaxCount = minInstanceBufferCount;
		}

		// Gather instances to be drawn
		uint32_t instanceIndex{ 0 };

		// Monsters
		for (auto i = 0; i < game.monsters.size(); i++) {
			Game::Entities::Monster& monster = game.monsters[i];
			if (monster.state == Game::Entities::State::Dead) {
				continue;
			}
			InstanceData& instance = frame.instances[instanceIndex++];
			instance.imageIndex = monster.imageIndex;
			instance.pos = glm::vec3(monster.position, 0.0f);
			instance.scale = monster.scale;
			instance.effect = static_cast<uint32_t>(monster.effect);
		}

		// Projectiles (@todo: maybe separate into own instance buffer due to diff. update frequency)
		for (auto i = 0; i < game.projectiles.size(); i++) {
			Game::Entities::Projectile& projectile = game.projectiles[i];
			if (projectile.state == Game::Entities::State::Dead) {
				continue;
			}
			InstanceData& instance = frame.instances[instanceIndex++];
			instance.imageIndex = projectile.imageIndex;
			instance.pos = glm::vec3(projectile.position, 0.0f);
			instance.scale = projectile.scale;
			instance.effect = static_cast<uint32_t>(projectile.effect);
		}

		// Pickups (@todo: maybe separate into own instance buffer due to diff. update frequency)
		for (auto i = 0; i < game.pickups.size(); i++) {
			Game::Entities::Pickup& pickup = game.pickups[i];
			if (pickup.state == Game::Entities::State::Dead) {
				continue;
			}
			InstanceData& instance = frame.instances[instanceIndex++];
			instance.imageIndex = pickup.imageIndex;
			instance.pos = glm::vec3(pickup.position, 0.0f);
			instance.scale = pickup.scale;
			instance.effect = static_cast<uint32_t>(pickup.effect);
		}

		// Numbers (@todo: maybe separate into own instance buffer due to diff. update frequency)
		for (auto i = 0; i < game.numbers.size(); i++) {
			Game::Entities::Number& number = game.numbers[i];
			if (number.state == Game::Entities::State::Dead) {
				continue;
			}
			// Draw one instance per number digit
			for (auto i = 0; i < number.digits; i++) {
				InstanceData& instance = frame.instances[instanceIndex++];
				const char v = number.stringValue[i];
				instance.imageIndex = game.firstNumberImageIndex + std::atoi(&v);
				// @todo: center
				instance.pos = glm::vec3(number.position + glm::vec2(i * number.scale * 0.6f, 0.0f), 0.0f);
				instance.scale = number.scale;
				instance.effect = static_cast<uint32_t>(number.effect);
			}
		}

		// Player
		frame.instances[instanceIndex] = {
			.pos = glm::vec3(game.player.position, 0.0f),
			.scale = game.player.scale,
			.imageIndex = game.player.imageIndex,
			.effect = static_cast<uint32_t>(game.player.effect)
		};

		frame.instanceBufferDrawCount = instanceIndex + 1;
		
		assert(frame.instanceBufferDrawCount > 0);

		const size_t instanceBufferSize = frame.instanceBufferDrawCount * sizeof(InstanceData);
#if defined(USE_REBAR)
		memcpy(frame.instanceBuffer->mapped, &frame.instances[0], instanceBufferSize);
#else
		stagingBuffer->copyTo(frame.instances, instanceBufferSize);
		if (!copyCommandBuffer) {
			copyCommandBuffer = new CommandBuffer({ .device = *vulkanDevice, .pool = commandPool });
		}
		copyCommandBuffer->begin();
		VkBufferCopy bufferCopy = { .size = instanceBufferSize };
		vkCmdCopyBuffer(copyCommandBuffer->handle, stagingBuffer->buffer, frame.instanceBuffer->buffer, 1, &bufferCopy);
		copyCommandBuffer->end();
		copyCommandBuffer->oneTimeSubmit(queue);
#endif
		frame.instanceBufferSize = instanceBufferSize;
	}

	void updateLightsBuffer(FrameObjects& frame)
	{
		const uint32_t maxLightsCount =
			static_cast<uint32_t>(game.projectiles.size()) +
			// Player light
			1;

		// Only recreate buffer if necessary, resizing is done in "chunks" to avoid frequent resizes
		const int32_t minLightBufferCount = std::max(maxLightsCount + instanceBufferBlockSizeIncrease - 1 - (maxLightsCount + instanceBufferBlockSizeIncrease - 1) % instanceBufferBlockSizeIncrease, instanceBufferBlockSizeIncrease);
		if (frame.lightsBufferMaxCount < minLightBufferCount) {
			std::cout << "Resizing lights buffer for frame " << frame.index << " to " << minLightBufferCount << " elements\n";
			// Host
			delete[] frame.lights;
			frame.lights = new LightSource[minLightBufferCount];
			// Device
			delete frame.lightsBuffer;
			frame.lightsBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				.size = minLightBufferCount * sizeof(LightSource),
#if defined(USE_REBAR)
				.vmaAllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.map = true,
#endif
				});
#if defined(USE_REBAR)
			VkMemoryPropertyFlags memPropFlags;
			vmaGetAllocationMemoryProperties(VulkanContext::vmaAllocator, frame.lightsBuffer->bufferAllocation, &memPropFlags);
			// @todo: fall back to staging if no ReBAR
			assert(memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
#endif
			frame.lightsBufferMaxCount = minLightBufferCount;
		}

		// Gather lights to be drawn
		uint32_t lightIndex{ 0 };

		// Post process uses a different aspect ratio than internal rendering, which needs to be taken into account
		float postProcessAR = (float)width / ((float)height * 4.0f / 3.0f);

		// Projectiles (@todo: maybe separate into own light buffer due to diff. update frequency)
		for (auto i = 0; i < game.projectiles.size(); i++) {
			Game::Entities::Projectile& projectile = game.projectiles[i];
			if (projectile.state == Game::Entities::State::Dead) {
				continue;
			}
			// Must be relative to the player (which in turn is centered on the screen)
			glm::vec2 lPos = projectile.position - game.player.position;
			lPos /= (screenDim * 2.0f);
			lPos.x /= postProcessAR;
			lPos += 0.5;
			frame.lights[lightIndex++] = {
				.pos = lPos,
				.color = projectile.lightColor,
				.radius = 0.04f,
			};
		}

		// Player
		frame.lights[lightIndex++] = {
			.pos = glm::vec2(0.5),
			.color = glm::vec3(1.0f),
			.radius = 0.2f,
		};

		frame.lightsBufferDrawCount = lightIndex;

		assert(frame.lightsBufferDrawCount > 0);

		const size_t lightBufferSize = frame.lightsBufferDrawCount * sizeof(LightSource);
#if defined(USE_REBAR)
		memcpy(frame.lightsBuffer->mapped, &frame.lights[0], lightBufferSize);
#else
		stagingBuffer->copyTo(frame.lights, lightsBufferSize);
		if (!copyCommandBuffer) {
			copyCommandBuffer = new CommandBuffer({ .device = *vulkanDevice, .pool = commandPool });
		}
		copyCommandBuffer->begin();
		VkBufferCopy bufferCopy = { .size = lightBufferSize };
		vkCmdCopyBuffer(copyCommandBuffer->handle, stagingBuffer->buffer, frame.lightsBuffer->buffer, 1, &bufferCopy);
		copyCommandBuffer->end();
		copyCommandBuffer->oneTimeSubmit(queue);
#endif
		frame.lightsBufferSize = lightBufferSize;		

		if (!frame.descriptorSetLights) {
			frame.descriptorSetLights = new DescriptorSet({
				.pool = descriptorPool,
				.layouts = { descriptorSetLayoutLights->handle },
				.descriptors = {
					{.dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &frame.lightsBuffer->descriptor }
				}
			});
		}
		else {
			frame.descriptorSetLights->updateDescriptor(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &frame.lightsBuffer->descriptor);
		}
	}

	// Game UI (not ImGui debug UI)
	void updateUIBuffer(FrameObjects& frame) {
		const glm::vec2 origin{ -0.95f };
		
		std::vector<Vertex> v{};

		// @todo: reserve instead of push_back
		auto addElement = [&v](glm::vec4 r, glm::vec4 uv, float z = 0.0f) {
			// x:top y:left, z:bottom w:right
			v.push_back({ { r.w, r.z, z }, { uv.w, uv.z } });
			v.push_back({ { r.y, r.z, z }, { uv.y, uv.z } });
			v.push_back({ { r.y, r.x, z }, { uv.y, uv.x } });
			v.push_back({ { r.y, r.x, z }, { uv.y, uv.x } });
			v.push_back({ { r.w, r.x, z }, { uv.w, uv.x } });
			v.push_back({ { r.w, r.z, z }, { uv.w, uv.z } });
		};

		auto t = 1.0f / 4.0f;
		auto h = 0.025f;
		glm::vec2 o = origin;

		addElement({ o.y + 0.0f, o.x + 0.0f, o.y + h, o.x + 0.25f }, { 0.0f, 0.0f, t, 1.0f });
		addElement({ o.y + 0.0f, o.x + 0.0f, o.y + h, o.x + 0.25f * game.player.health / game.player.maxHealth }, { t, 0.0f, t + t, 1.0f });

		o.y += h * 2.0f;
		
		addElement({ o.y + 0.0f, o.x + 0.0f, o.y + h, o.x + 0.25f }, { 0.0f, 0.0f, t, 1.0f });
		addElement({ o.y + 0.0f, o.x + 0.0f, o.y + h, o.x + 0.25f * game.player.stamina / game.player.maxStamina }, { t * 3, 0.0f, t * 3 + t , 1.0f });

		o.y += h * 2.0f;

		addElement({ o.y + 0.0f, o.x + 0.0f, o.y + h, o.x + 0.25f }, { 0.0f, 0.0f, t, 1.0f });
		addElement({ o.y + 0.0f, o.x + 0.0f, o.y + h, o.x + 0.25f * (game.player.experience - game.getNextLevelExp(game.player.level)) / game.getNextLevelExp(game.player.level + 1) }, { t * 2, 0.0f, t * 2 + t , 1.0f });

		frame.uiBufferVertexCount = static_cast<uint32_t>(v.size());

		const size_t vertexBufferSize = v.size() * sizeof(Vertex);

		if (frame.uiBufferSize < vertexBufferSize) {
			delete frame.uiBuffer;
			frame.uiBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				.size = vertexBufferSize,
#if defined(USE_REBAR)
				.vmaAllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.map = true,
#endif
			});
			frame.uiBufferSize = vertexBufferSize;
		}
#if defined(USE_REBAR)
		memcpy(frame.uiBuffer->mapped, v.data(), vertexBufferSize);
#else
		// todo
#endif

		// Text elements are separate

		// @todo
		ui.textElements.clear();
		// @todo: test
		ui.textElements.push_back({
			.pos = glm::vec2(0.0f, 0.2f),
			.text = std::format("Killed: {}", game.currentRun.monstersKilled)
		});
		ui.textElements.push_back({
			.pos = glm::vec2(0.0f, 0.25f),
			.text = std::format("Run: {}m {}s ", static_cast<int32_t>(floor(game.currentRun.duration)) / 60, static_cast<int32_t>(floor(game.currentRun.duration)) % 60)
		});
		ui.textElements.push_back({
			.pos = glm::vec2(0.0f, 0.3f),
			.text = std::format("{} fps", lastFPS)
		});

		if (editor.active) {
			ui.textElements.push_back({
				.pos = glm::vec2(0.0f, 0.35f),
				.text = "Editor mode enabled (Toggle with F2)"
			});
			ui.textElements.push_back({
				.pos = glm::vec2(0.0f, 0.4f),
				.text = std::format("Layer: {} (Toggle with F3)", editor.activeLayer)
			});
			ui.textElements.push_back({
				.pos = glm::vec2(0.0f, 0.45f),
				.text = std::format("Tileindex: {}", editor.tileIndex)
			});
		}

		std::vector<Vertex> tv{};

		const uint32_t texWidth = 399;
		const uint32_t texHeight = 404;

		if (ui.textElements.size() > 0) {
			for (auto& elem : ui.textElements) {
				float posx = origin.x + elem.pos.x;				
				for (auto i = 0; i < elem.text.size(); i++) {
					bmchar* charInfo = &fontChars[(int)elem.text[i]];
					if (charInfo->width == 0) {
						charInfo->width = 36;
					}
					const float sc = 36.0f * 72.0f;
					float charw = ((float)(charInfo->width) / sc);
					float dimx = charw;
					float charh = ((float)(charInfo->height) / sc);
					float dimy = charh;
					float us = charInfo->x / (float)texWidth;
					float ue = (charInfo->x + charInfo->width) / (float)texWidth;
					float ts = charInfo->y / (float)texHeight;
					float te = (charInfo->y + charInfo->height) / (float)texHeight;
					float xo = charInfo->xoffset / sc;
					float yo = charInfo->yoffset / sc;
					float posy = origin.y + elem.pos.y + yo;
					tv.push_back({ { posx + dimx + xo,  posy + dimy, 0.0f }, { ue, te } });
					tv.push_back({ { posx + xo,         posy + dimy, 0.0f }, { us, te } });
					tv.push_back({ { posx + xo,         posy,        0.0f }, { us, ts } });
					tv.push_back({ { posx + xo,         posy,        0.0f }, { us, ts } });
					tv.push_back({ { posx + dimx + xo,  posy,        0.0f }, { ue, ts } });
					tv.push_back({ { posx + dimx + xo,  posy + dimy, 0.0f }, { ue, te } });
					float advance = ((float)(charInfo->xadvance) / sc);
					posx += advance;
				}
			}
		}

		const size_t textVertexBufferSize = tv.size() * sizeof(Vertex);

		if (frame.uiTextBufferSize < textVertexBufferSize) {
			delete frame.uiTextBuffer;
			frame.uiTextBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				.size = textVertexBufferSize,
				.vmaAllocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.map = true,
			});
			frame.uiTextBufferSize = textVertexBufferSize;
		}
		frame.uiTextBufferVertexCount = static_cast<uint32_t>(tv.size());
		memcpy(frame.uiTextBuffer->mapped, tv.data(), textVertexBufferSize);
	}

	void prepare() {
		VulkanApplication::prepare();

		if (commandLineParser.isSet("editormode")) {
			editor.active = true;
		}

		// Create one large staging buffer to be reused for copies
		stagingBuffer = new Buffer({
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.size = stagingBufferSize,
			.map = true
		});

		fileWatcher = new FileWatcher();

		game.playFieldSize = screenDim;

		loadAssets();
		generateQuad();
		initTileMap();

		// Init player
		auto& player = game.player;
		player.speed = 5.0f;
		player.scale = 1.0f;
		player.position = glm::vec2((float)game.tilemap.width / 2.0f, (float)game.tilemap.height / 2.0f);
		// @todo: Proper weapon setup/selection
		player.weapons.resize(1);
		player.weapons[0] = game.playerWeaponTypes[2];

		editor.pos = glm::ivec2(game.tilemap.width / 2, (float)game.tilemap.height / 2);

		// @todo: for benchmarking, this is > 60 fps on my setup
		//spawnMonsters(1150000);
		game.spawnMonsters(game.spawnTriggerMonsterCount);

		// @todo: move camera out of vulkanapplication (so we can have multiple cameras)
		camera.type = Camera::CameraType::firstperson;

		frameObjects.resize(getFrameCount());
		size_t frameIdx = 0;
		for (FrameObjects& frame : frameObjects) {
			createBaseFrameObjects(frame);
			frame.index = frameIdx++;
			frameObjects.resize(getFrameCount());
			frame.uniformBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				.size = sizeof(ShaderData),
				.map = true
			});
		}

		descriptorPool = new DescriptorPool({
			.name = "Application descriptor pool",
			// @todo
			.maxSets = 32,
//			.maxSets = getFrameCount() + 2,
			.poolSizes = {
				{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 8 /*getFrameCount()*/ },
				{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 4096 /*@todo*/},
				{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 256 /*@todo*/},
				{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 4 /*@todo*/},
				{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 4 /*@todo*/},
			}
		});

		descriptorSetLayoutUniforms = new DescriptorSetLayout({
			.bindings = {
				{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
			}
		});

		for (FrameObjects& frame : frameObjects) {
			frame.descriptorSet = new DescriptorSet({
				.pool = descriptorPool,
				.layouts = { descriptorSetLayoutUniforms->handle },
				.descriptors = {
					{.dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &frame.uniformBuffer->descriptor },
				}
			});
		}

		descriptorSetLayoutLights = new DescriptorSetLayout({
			.bindings = {
				{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
			}
		});
		
		updateTextureDescriptor();

		VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &swapChain->colorFormat,
			.depthAttachmentFormat = depthFormat,
			.stencilAttachmentFormat = depthFormat
		};

		VkPipelineColorBlendAttachmentState blendAttachmentState{
			.colorWriteMask = 0xf
		};

		// Sprites

		pipelineLayouts["sprite"] = new PipelineLayout({
			.layouts = { descriptorSetLayoutTextures->handle, descriptorSetLayoutSamplers->handle, descriptorSetLayoutUniforms->handle },
		});

		PipelineVertexInput vertexInput = {
			.bindings = {
				{ .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
				{ .binding = 1, .stride = sizeof(InstanceData), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE }
			},
			.attributes = {
				{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
				{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
				// Instanced
				{ .location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(InstanceData, pos) },
				{ .location = 3, .binding = 1, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(InstanceData, scale) },
				{ .location = 4, .binding = 1, .format = VK_FORMAT_R32_UINT, .offset = offsetof(InstanceData, imageIndex) },
				{ .location = 5, .binding = 1, .format = VK_FORMAT_R32_UINT, .offset = offsetof(InstanceData, effect) },
			}
		};

		pipelines["sprite"] = new Pipeline({
			.shaders = {
				.filename = getAssetPath() + "shaders/sprite.slang",
				.stages = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT }
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["sprite"],
			.vertexInput = vertexInput,
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			},
			.blending = {
				.attachments = { blendAttachmentState }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = {
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &swapChain->colorFormat,
				.depthAttachmentFormat = depthFormat,
				.stencilAttachmentFormat = depthFormat
			},
			.enableHotReload = true
		});
 		pipelineList.push_back(pipelines["sprite"]);

		// Tilemap
		pipelineLayouts["tilemap"] = new PipelineLayout({
			.layouts = { descriptorSetLayoutTextures->handle, descriptorSetLayoutSamplers->handle, descriptorSetLayoutUniforms->handle },
			// For debug viz
			.pushConstantRanges = {
				{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(uint32_t) * 2}
			}
		});

		vertexInput = {
			.bindings = {
				{.binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
				{.binding = 1, .stride = sizeof(TilemapInstanceData), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE },
			},
			.attributes = {
				{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
				{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
				// Instanced
				{.location = 2, .binding = 1, .format = VK_FORMAT_R32G32_UINT, .offset = offsetof(TilemapInstanceData, pos) },
				{.location = 3, .binding = 1, .format = VK_FORMAT_R32_UINT, .offset = offsetof(TilemapInstanceData, imageIndex) },
				{.location = 4, .binding = 1, .format = VK_FORMAT_R32_UINT, .offset = offsetof(TilemapInstanceData, effect) },
			}
		};

		pipelines["tilemap"] = new Pipeline({
			.shaders = {
				.filename = getAssetPath() + "shaders/tilemap.slang",
				.stages = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT }
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["tilemap"],
			.vertexInput = vertexInput,
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			},
			.blending = {
				.attachments = { blendAttachmentState }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = {
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &swapChain->colorFormat,
				.depthAttachmentFormat = depthFormat,
				.stencilAttachmentFormat = depthFormat
			},
			.enableHotReload = true
			});
		pipelineList.push_back(pipelines["tilemap"]);
		const uint32_t layerIndex = 1;
		pipelines["tilemap_foreground"] = new Pipeline({
			.shaders = {
				.filename = getAssetPath() + "shaders/tilemap.slang",
				.stages = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT }
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["tilemap"],
			.vertexInput = vertexInput,
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			},
			.blending = {
				.attachments = { blendAttachmentState }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = {
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &swapChain->colorFormat,
				.depthAttachmentFormat = depthFormat,
				.stencilAttachmentFormat = depthFormat
			},
			.specialization = {
				.entries = {
					VkSpecializationMapEntry { .constantID = 0, .size = sizeof(uint32_t) },
				},
				.dataSize = sizeof(uint32_t),
				.data = &layerIndex
			},
			.enableHotReload = true
		});
		pipelineList.push_back(pipelines["tilemap_foreground"]);


		// CRT frame
		VkPipelineColorBlendAttachmentState blendAttachmentStateEnabled{
			.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_ADD,
			.colorWriteMask = 0xf,
		};

		pipelineLayouts["crtframe"] = new PipelineLayout({
			.layouts = { descriptorSetLayoutTextures->handle, descriptorSetLayoutSamplers->handle, descriptorSetLayoutUniforms->handle },
			// Index of the crt frame is passed via push constant
			.pushConstantRanges = {
				{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(uint32_t) }
			}
		});

		pipelines["crtframe"] = new Pipeline({
			.shaders = {
				.filename = getAssetPath() + "shaders/frame.slang",
				.stages = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT }
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["crtframe"],
			//.vertexInput = vertexInput,
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			},
			.blending = {
				.attachments = { blendAttachmentStateEnabled }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = {
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &swapChain->colorFormat,
				.depthAttachmentFormat = depthFormat,
				.stencilAttachmentFormat = depthFormat
			},
			.enableHotReload = true
		});
		pipelineList.push_back(pipelines["crtframe"]);
		// In-Game UI (not ImGui debug UI)

		vertexInput = {
			.bindings = {
				{.binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
			},
			.attributes = {
				{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
				{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
			}
		};

		pipelineLayouts["gameui"] = new PipelineLayout({
			.layouts = { descriptorSetLayoutTextures->handle, descriptorSetLayoutSamplers->handle, descriptorSetLayoutUniforms->handle },
			.pushConstantRanges = {
				{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(uint32_t) }
			}
		});

		pipelines["gameui"] = new Pipeline({
			.shaders = {
				.filename = getAssetPath() + "shaders/ui.slang",
				.stages = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT }
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["gameui"],
			.vertexInput = vertexInput,
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = VK_COMPARE_OP_ALWAYS,
			},
			.blending = {
				.attachments = { blendAttachmentState }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = {
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &swapChain->colorFormat,
				.depthAttachmentFormat = depthFormat,
				.stencilAttachmentFormat = depthFormat
			},
			.enableHotReload = true
		});
		pipelineList.push_back(pipelines["gameui"]);
		VkPipelineColorBlendAttachmentState blendAttachmentStateUiText{
			.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_ADD,
			.colorWriteMask = 0xf,
		};

		pipelines["gameuitext"] = new Pipeline({
			.shaders = {
				.filename = getAssetPath() + "shaders/uitext.slang",
				.stages = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT }
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["gameui"],
			.vertexInput = vertexInput,
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = VK_COMPARE_OP_ALWAYS,
			},
			.blending = {
				.attachments = { blendAttachmentStateUiText }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = {
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &swapChain->colorFormat,
				.depthAttachmentFormat = depthFormat,
				.stencilAttachmentFormat = depthFormat
			},
			.enableHotReload = true
			});
		pipelineList.push_back(pipelines["gameui"]);
		// Post process
		descriptorSetLayoutRenderImage = new DescriptorSetLayout({
			.descriptorIndexing = true,
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT}
			}
		});

		SamplerCreateInfo samplerCI{
			.name = "Post process sampler",
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		};
		renderImageSampler = new Sampler(samplerCI);

		VkDescriptorImageInfo renderImageDesc{
			.sampler = renderImageSampler->handle,
			.imageView = renderImage.view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		descriptorSetRenderImage = new DescriptorSet({
			.pool = descriptorPool,
			.layouts = { descriptorSetLayoutRenderImage->handle, descriptorSetLayoutLights->handle },
			.descriptors = {
				{.dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &renderImageDesc}
			}
		});

		pipelineLayouts["postprocess"] = new PipelineLayout({
			.layouts = { descriptorSetLayoutUniforms->handle, descriptorSetLayoutRenderImage->handle, descriptorSetLayoutLights->handle },
			// Used to select the current post process effect
			.pushConstantRanges = {
				{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(int32_t)}
			}
		});

		pipelines["postprocess"] = new Pipeline({
			.shaders = {
				.filename = getAssetPath() + "shaders/postprocess.slang",
				.stages = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT }
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["postprocess"],
			//.vertexInput = vertexInput,
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			},
			.blending = {
				.attachments = { blendAttachmentState }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = {
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &swapChain->colorFormat,
				.depthAttachmentFormat = depthFormat,
				.stencilAttachmentFormat = depthFormat
			},
			.enableHotReload = true
		});
		pipelineList.push_back(pipelines["postprocess"]);

		for (auto& pipeline : pipelineList) {
			fileWatcher->addPipeline(pipeline);
		}
		fileWatcher->onFileChanged = [=](const std::string filename, const std::vector<void*> userdata) {
			this->onFileChanged(filename, userdata);
		};
		fileWatcher->start();

		// @todo
		if (backgroundMusic.openFromFile(getAssetPath() + "music/18._infinite_darkness.mp3")) {
			backgroundMusic.setVolume(30);
			backgroundMusic.setLoop(true);
			backgroundMusic.play();
		} else {
			std::cout << "Could not load background music track\n";
		}

		setPostProcessEffect(PostProcessEffect::FadeIn);

		overlay->visible = false;

		prepared = true;
	}

	void recordCommandBuffer(FrameObjects& frame)
	{
		ZoneScopedN("Command buffer recording");

		const bool multiSampling = (settings.sampleCount > VK_SAMPLE_COUNT_1_BIT);

		CommandBuffer* cb = frame.commandBuffer;
		cb->begin();

		// New structures are used to define the attachments used in dynamic rendering
		VkRenderingAttachmentInfo colorAttachment{};
		VkRenderingAttachmentInfo depthStencilAttachment{};		

		// Transition color and depth images for drawing
		cb->insertImageMemoryBarrier(
			swapChain->buffers[swapChain->currentImageIndex].image,
			0,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		cb->insertImageMemoryBarrier(
			renderImage.image,
			0,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		cb->insertImageMemoryBarrier(
			depthStencil.image,
			0,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 });

		// New structures are used to define the attachments used in dynamic rendering
		colorAttachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
			.imageView = multiSampling ? multisampleTarget.color.view : renderImage.view,
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = {.color = { 0.0f, 0.0f, 0.0f, 0.0f } },
		};
		if (multiSampling) {
			colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			colorAttachment.resolveImageView = swapChain->buffers[swapChain->currentImageIndex].view;
			colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		}

		// A single depth stencil attachment info can be used, but they can also be specified separately.
		// When both are specified separately, the only requirement is that the image view is identical.			
		depthStencilAttachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
			.imageView = multiSampling ? multisampleTarget.depth.view : depthStencil.view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.clearValue = {.depthStencil = {1.0f,  0} },
		};
		if (multiSampling) {
			depthStencilAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			depthStencilAttachment.resolveImageView = depthStencil.view;
			depthStencilAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
		}

		VkRenderingInfo renderingInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
			.renderArea = { 0, 0, width, height },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachment,
			.pDepthAttachment = &depthStencilAttachment,
			.pStencilAttachment = &depthStencilAttachment
		};

		cb->beginRendering(renderingInfo);
		// Game uses a fixed 4:3 aspect ratio for now
		float vpHeight = (float)height;
		float vpWidth = vpHeight * 4.0f / 3.0f;
		float vpLeft = ((float)width - vpWidth) / 2.0f;
		cb->setViewport(vpLeft, 0.0f, vpWidth, vpHeight, 0.0f, 1.0f);
		cb->setScissor(0, 0, width, height);
	
		// Tilemap
		cb->bindVertexBuffers(0, 1, { quadBuffer->buffer });
		cb->bindVertexBuffers(1, 1, { frame.tilemapInstanceBuffer->buffer });
		cb->bindDescriptorSets(pipelineLayouts["tilemap"], { descriptorSetTextures, descriptorSetSamplers, frame.descriptorSet });
		// @todo: only edit mode
		glm::ivec2 selectedTile{ -1 };
		if (editor.active) {
			selectedTile = editor.selectedTile;
		} else {
			selectedTile = { -1, -1 };
			//selectedTile = game.player.tilePos();
		}
		cb->updatePushConstant(pipelineLayouts["tilemap"], 0, &selectedTile);
		cb->bindPipeline(pipelines["tilemap"]);
		cb->draw(6, frame.tilemapInstanceCount, 0, 0);
		if (frame.tilemapForegroundInstanceCount > 0) {
			cb->bindPipeline(pipelines["tilemap_foreground"]);
			cb->bindVertexBuffers(1, 1, { frame.tilemapInstanceBuffer->buffer }, { frame.tilemapInstanceCount * sizeof(TilemapInstanceData) });
			cb->draw(6, frame.tilemapForegroundInstanceCount, 0, 0);
		}

		// Draw sprites using instancing
		// Instancing buffer stores sprite index, position, scale, direction (to flip/rotate) uv, maybe color for health state
		if (!editor.active) {
			cb->bindVertexBuffers(0, 1, { quadBuffer->buffer });
			cb->bindVertexBuffers(1, 1, { frame.instanceBuffer->buffer });
			cb->bindDescriptorSets(pipelineLayouts["sprite"], { descriptorSetTextures, descriptorSetSamplers, frame.descriptorSet });
			cb->bindPipeline(pipelines["sprite"]);
			cb->draw(6, frame.instanceBufferDrawCount, 0, 0);
		}

		// Game overlay
		// @todo: before or after post process?
		cb->bindVertexBuffers(0, 1, { frame.uiBuffer->buffer });
		cb->bindDescriptorSets(pipelineLayouts["gameui"], { descriptorSetTextures, descriptorSetSamplers, frame.descriptorSet });
		cb->bindPipeline(pipelines["gameui"]);
		cb->updatePushConstant(pipelineLayouts["gameui"], 0, &game.uiImageIndex);
		cb->draw(frame.uiBufferVertexCount, 1, 0, 0);
		// Text
		// @todo: Separate pipeline?
		if (frame.uiTextBufferVertexCount > 0) {
			cb->bindVertexBuffers(0, 1, { frame.uiTextBuffer->buffer });
			cb->bindPipeline(pipelines["gameuitext"]);
			cb->updatePushConstant(pipelineLayouts["gameui"], 0, &fontImageIndex);
			cb->draw(frame.uiTextBufferVertexCount, 1, 0, 0);
		}

		cb->endRendering();

		// Transition color image for presentation
		cb->insertImageMemoryBarrier(
			renderImage.image,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		colorAttachment.imageView = swapChain->buffers[swapChain->currentImageIndex].view;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		cb->beginRendering(renderingInfo);
		cb->setViewport(0.0f, 0.0f, width, height, 0.0f, 1.0f);

		// Post process
		cb->bindDescriptorSets(pipelineLayouts["postprocess"], { frame.descriptorSet, descriptorSetRenderImage, frame.descriptorSetLights });
		cb->bindPipeline(pipelines["postprocess"]);
		if (editor.active) {
			const uint32_t pc = 999;
			cb->updatePushConstant(pipelineLayouts["postprocess"], 0, &pc);
		} else {
			cb->updatePushConstant(pipelineLayouts["postprocess"], 0, &postProcessEffect);
		}
		cb->draw(3, 1, 0, 0);

		// Backdrop
		cb->bindDescriptorSets(pipelineLayouts["crtframe"], { descriptorSetTextures, descriptorSetSamplers, frame.descriptorSet });
		cb->bindPipeline(pipelines["crtframe"]);
		cb->updatePushConstant(pipelineLayouts["crtframe"], 0, &crtFrameImageIndex);
		cb->draw(3, 1, 0, 0);
		if (overlay->visible) {
			overlay->draw(cb, getCurrentFrameIndex());
		}
		cb->endRendering();

		// Transition color image for presentation
		cb->insertImageMemoryBarrier(
			swapChain->buffers[swapChain->currentImageIndex].image,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			0,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		cb->end();
	}

	void handleEvent(sf::Event& event) {
		if (event.type == sf::Event::KeyPressed) {
			if (event.key.code == sf::Keyboard::F2) {
				editor.active = !editor.active;
			}
			if (editor.active) {
				if (event.key.code == sf::Keyboard::F3) {
					editor.activeLayer = !editor.activeLayer;
				}
				if ((event.key.code == sf::Keyboard::Subtract) && (editor.tileIndex > 0)) {
					editor.tileIndex -= 1;
				}
				if ((event.key.code == sf::Keyboard::Add) && (editor.tileIndex < game.tilemap.lastTileIndex - game.tilemap.firstTileIndex)) {
					editor.tileIndex += 1;
				}
				if ((event.key.code == sf::Keyboard::Delete) && (editor.activeLayer == 1)) {
					if (editor.selectedTile.x > -1 && editor.selectedTile.x < TILEMAP_MAX_DIM && editor.selectedTile.y > -1 && editor.selectedTile.y < TILEMAP_MAX_DIM) {
						game.tilemap.foregroundLayer[editor.selectedTile.x][editor.selectedTile.y] = -1;
					};
				}
			}
		}
		if (event.type == sf::Event::MouseWheelScrolled) {
			if (editor.active) {
				screenDim.x *= 1.0 - ((float)event.mouseWheelScroll.delta * 0.1);
				screenDim.y *= 1.0 - ((float)event.mouseWheelScroll.delta * 0.1);
			}
		}
	}

	void render() {
		ZoneScoped;

		camera.viewportSize = glm::uvec2(width, height);

		camera.mouse.buttons.left = mouseButtons.left;
		camera.mouse.cursorPos = mousePos;
		camera.mouse.cursorPosNDC = (mousePos / glm::vec2(float(width), float(height)));

		FrameObjects& currentFrame = frameObjects[getCurrentFrameIndex()];
		VulkanApplication::prepareFrame(currentFrame);
		updateOverlay(getCurrentFrameIndex());
		// @todo
		{
			ZoneScopedN("Game update");
			if (!paused) {
				game.update(frameTimer);
				game.updateInput(frameTimer);
			// @todo
			if (editor.active) {
				glm::vec2 direction = glm::vec2(.0f, .0f);
				if (sf::Keyboard::isKeyPressed(sf::Keyboard::A)) {
					direction.x = -1.0f;
				}
				if (sf::Keyboard::isKeyPressed(sf::Keyboard::D)) {
					direction.x = 1.0f;
				}
				if (sf::Keyboard::isKeyPressed(sf::Keyboard::W)) {
					direction.y = -1.0f;
				}
				if (sf::Keyboard::isKeyPressed(sf::Keyboard::S)) {
					direction.y = 1.0f;
				}
				editor.pos += direction * 25.0f * frameTimer;
				auto mo = camera.mouse.cursorPosNDC - glm::vec2(0.5f);
				mo *= glm::vec2(screenDim.x * 2.0f, screenDim.y * 2.0f);
				editor.selectedTile = glm::ivec2(editor.pos + mo + glm::vec2(0.5));
				if ((!overlay->visible) && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
					if (editor.selectedTile.x > -1 && editor.selectedTile.x < TILEMAP_MAX_DIM && editor.selectedTile.y > -1 && editor.selectedTile.y < TILEMAP_MAX_DIM) {
						switch (editor.activeLayer) {
						case 0:
							game.tilemap.backgroundLayer[editor.selectedTile.x][editor.selectedTile.y] = editor.tileIndex;
							break;
						case 1:
							game.tilemap.foregroundLayer[editor.selectedTile.x][editor.selectedTile.y] = editor.tileIndex;
							break;
						}
					}
				}
			}
		}
		{
			ZoneScopedN("Instance buffer update");
			updateInstanceBuffer(currentFrame);
		}
		{
			ZoneScopedN("Tilemap buffer update");
			updateTileMap(currentFrame);
		}
		{
			ZoneScopedN("Lights buffer update");
			updateLightsBuffer(currentFrame);
		}
		{
			ZoneScopedN("UI buffer update");
			updateUIBuffer(currentFrame);
		}


		updatePostProcessEffect(frameTimer);

		shaderData.timer = timer;
		if (editor.active) {
			shaderData.mvp = glm::translate(glm::mat4(1.0f), -glm::vec3(glm::vec2(editor.pos) / screenDim, 0.0f));
		} else {
			shaderData.mvp = glm::translate(glm::mat4(1.0f), -glm::vec3(game.player.position / screenDim, 0.0f));
		}
		shaderData.mvp *= glm::ortho(-screenDim.x, screenDim.x, -screenDim.x, screenDim.x);
		shaderData.screenRes = glm::vec2((float)width, (float)height);
		shaderData.lightCount = currentFrame.lightsBufferDrawCount;
		shaderData.dayNightCycle = game.dayNightCycle <= 1.0f ? game.dayNightCycle : 2.0 - game.dayNightCycle;
		float vpHeight = (float)height;
		float vpWidth = vpHeight * 4.0f / 3.0f;
		shaderData.viewportAR = (4.0f / 3.0f) * ((float)width/vpWidth);
		memcpy(currentFrame.uniformBuffer->mapped, &shaderData, sizeof(ShaderData)); // @todo: buffer function

		recordCommandBuffer(currentFrame);
		VulkanApplication::submitFrame(currentFrame);

		for (auto& pipeline : pipelineList) {
			if (pipeline->wantsReload) {
				pipeline->reload();
			}
		}
	}

	void windowResized() override
	{
		VkDescriptorImageInfo renderImageDesc{
			.sampler = renderImageSampler->handle,
			.imageView = renderImage.view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};
		descriptorSetRenderImage->updateDescriptor(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &renderImageDesc, 1);
	}

	void OnUpdateOverlay(vks::UIOverlay& overlay) {
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(0, 90), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Performance");
		ImGui::TextUnformatted(vulkanDevice->properties.deviceName);
		ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / lastFPS), lastFPS);
		ImGui::End();

		ImGui::Begin("Editor");
		if (ImGui::Button("Toggle")) {
			editor.active = !editor.active;
		}
		if (ImGui::Button("Save")) {
			game.tilemap.save("tilemap.bin");
		}
		if (ImGui::Button("Load")) {
			game.tilemap.load("tilemap.bin");
		}
		ImGui::End();
		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(0, 50), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Player");
		ImGui::Text("Pos: %.2f / %.2f", game.player.position.x, game.player.position.y);
		ImGui::Text("XP: %.2f / %d", game.player.experience, game.getNextLevelExp(game.player.level + 1));
		ImGui::Text("Level: %d", game.player.level);
		ImGui::Text("Crit chance: %.1f", game.player.criticalChance);
		ImGui::Text("Crit dmg: %.1f", game.player.criticalDamageMultiplier);
		ImGui::Text("Stamina: %.1f / %.1f", game.player.stamina, game.player.maxStamina);
		ImGui::End();

		ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(0, 50), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Statistics", 0, ImGuiWindowFlags_None);
		ImGui::Text("Monsters: %d", static_cast<uint32_t>(game.monsters.size()));
		ImGui::Text("Projectiles: %d", static_cast<uint32_t>(game.projectiles.size()));
		ImGui::Text("Pickups: %d", static_cast<uint32_t>(game.pickups.size()));
		ImGui::Text("Numbers: %d", static_cast<uint32_t>(game.numbers.size()));
		ImGui::End();
		ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(0, 50), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Current run", 0, ImGuiWindowFlags_None);
		ImGui::Text("Duration: %0.2d:%0.2d", static_cast<int32_t>(floor(game.currentRun.duration)) / 60, static_cast<int32_t>(floor(game.currentRun.duration)) % 60);
		ImGui::Text("Monsters killed: %d", game.currentRun.monstersKilled);
		ImGui::End();
	}

	void onFileChanged(const std::string filename, const std::vector<void*> owners) {
		std::cout << filename << " was modified\n";
		for (auto& owner : owners) {
			if (std::find(pipelineList.begin(), pipelineList.end(), owner) != pipelineList.end()) {
				static_cast<Pipeline*>(owner)->wantsReload = true;
			}
		}
	}

	virtual void keyPressed(uint32_t key)
	{
	}

};
Application* vulkanApplication;

// Main entry points

#if defined(_WIN32)
// Windows entry point
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowmd)
{
	for (int32_t i = 0; i < __argc; i++) { 
		VulkanApplication::args.push_back(__argv[i]); 
	};
	vulkanApplication = new Application();
	vulkanApplication->initVulkan();
	vulkanApplication->setupWindow();
	vulkanApplication->prepare();
	vulkanApplication->renderLoop();
	delete(vulkanApplication);
	return 0;
}

#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
// Android entry point

VulkanApplication *vulkanApplication;																
void android_main(android_app* state)																
{																									
	vulkanApplication = new VulkanApplication();													
	state->userData = vulkanApplication;															
	state->onAppCmd = vulkanApplication::handleAppCommand;											
	state->onInputEvent = vulkanApplication::handleAppInput;										
	androidApp = state;																				
	vks::android::getDeviceConfig();																
	vulkanApplication->renderLoop();																
	delete(vulkanApplication);																		
}

#elif defined(_DIRECT2DISPLAY)
// Linux entry point with direct to display wsi

VulkanApplication *vulkanApplication;																
static void handleEvent()                                											
{																									
}																									

int main(const int argc, const char *argv[])													    
{																									
	for (size_t i = 0; i < argc; i++) { vulkanApplication::args.push_back(argv[i]); };  			
	vulkanApplication = new VulkanApplication();													
	vulkanApplication->initVulkan();																
	vulkanApplication->prepare();																	
	vulkanApplication->renderLoop();																
	delete(vulkanApplication);																		
	return 0;																						
}

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)

	int main(const int argc, const char *argv[])												
{																								
	for (size_t i = 0; i < argc; i++) { vulkanApplication::args.push_back(argv[i]); };  		
	vulkanApplication = new VulkanApplication();												
	vulkanApplication->initVulkan();															
	vulkanApplication->setupWindow();					 										
	vulkanApplication->prepare();																
	vulkanApplication->renderLoop();															
	delete(vulkanApplication);																	
	return 0;																					
}

#elif defined(VK_USE_PLATFORM_XCB_KHR)

static void handleEvent(const xcb_generic_event_t *event)										
{																								
	if (vulkanApplication != NULL)																
	{																							
		vulkanApplication->handleEvent(event);													
	}																							
}				
	\
int main(const int argc, const char *argv[])													
{																								
	for (size_t i = 0; i < argc; i++) { vulkanApplication::args.push_back(argv[i]); };  		
	vulkanApplication = new VulkanApplication();												
	vulkanApplication->initVulkan();															
	vulkanApplication->setupWindow();					 										
	vulkanApplication->prepare();																
	vulkanApplication->renderLoop();															
	delete(vulkanApplication);																	
	return 0;																					
}

#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
#endif