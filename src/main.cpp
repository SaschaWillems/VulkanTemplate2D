/*
 * Copyright (C) 2023-2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "VulkanContext.h"
#include "FileWatcher.hpp"
#include <VulkanApplication.h>
#include "AudioManager.h"
#include "Texture.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <random>
#include "time.h"
#include <SFML/Audio.hpp>
#include <json.hpp>
#include "Game.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// @todo: audio (music and sfx)
// @todo: sync2 everywhere
// @todo: timeline semaphores

#define _USE_REBAR

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
	float tileMapSpeed;
} shaderData;

float tileMapSpeed{ 2.08f };

struct Vertex {
	float pos[3];
	float uv[2];
};

struct InstanceData {
	glm::vec3 pos;
	float scale{ 1.0f };
	uint32_t imageIndex{ 0 };
	uint32_t effect{ 0 };
};

struct TileMap {
	vks::Texture2D* texture{ nullptr };
	Sampler* sampler{ nullptr };
	DescriptorSet* descriptorSetSampler{ nullptr };
	uint32_t imageIndex;
	uint32_t firstTileIndex;
	uint32_t lastTileIndex;
	uint32_t width = 4096;
	uint32_t height = 4096;
};

Game::Game game;

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
		// @todo: Separate projectiles into own set of instance buffers (due to different update frequency?)
		//struct Projectiles {
		//	Buffer* instanceBuffer{ nullptr };
		//	uint32_t instanceBufferSize{ 0 };
		//	uint32_t instanceBufferDrawCount{ 0 };
		//} projectiles;
	};
	// One large staging buffer that's reused for all copies
	// @todo: per frame?
	const size_t stagingBufferSize = 64 * 1024 * 1024;
	Buffer* stagingBuffer{ nullptr };
	CommandBuffer* copyCommandBuffer{ nullptr };

	// One set for all images
	std::vector<VkDescriptorImageInfo> textureDescriptors{};
	std::vector<VkDescriptorImageInfo> samplerDescriptors{};
	std::vector<vks::Texture2D*> textures{};
	TileMap tileMap;
	Sampler* spriteSampler{ nullptr };
	
	std::vector<FrameObjects> frameObjects;
	FileWatcher* fileWatcher{ nullptr };
	DescriptorPool* descriptorPool;
	DescriptorSetLayout* descriptorSetLayoutUniforms;
	DescriptorSetLayout* descriptorSetLayoutSamplers;
	DescriptorSetLayout* descriptorSetLayoutTextures;
	DescriptorSet* descriptorSetTextures;
	DescriptorSet* descriptorSetSamplers;
	std::unordered_map<std::string, PipelineLayout*> pipelineLayouts;
	std::unordered_map<std::string, Pipeline*> pipelines;
	sf::Music backgroundMusic;
	Buffer* quadBuffer{ nullptr };
	glm::vec2 screenDim{ 0.0f };
public:	
	Application() : VulkanApplication() {
		apiVersion = VK_API_VERSION_1_3;

		Device::enabledFeatures.shaderClipDistance = VK_TRUE;
		Device::enabledFeatures.samplerAnisotropy = VK_TRUE;
		Device::enabledFeatures.depthClamp = VK_TRUE;
		Device::enabledFeatures.fillModeNonSolid = VK_TRUE;

		Device::enabledFeatures11.multiview = VK_TRUE;
		Device::enabledFeatures12.descriptorIndexing = VK_TRUE;
		Device::enabledFeatures12.runtimeDescriptorArray = VK_TRUE;
		Device::enabledFeatures12.descriptorBindingVariableDescriptorCount = VK_TRUE;
		Device::enabledFeatures13.dynamicRendering = VK_TRUE;

		settings.sampleCount = VK_SAMPLE_COUNT_4_BIT;

		audioManager = new AudioManager();

		slangCompiler = new SlangCompiler();

		// @todo: absolute or relative?
		const float aspectRatio = (float)width / (float)height;
		screenDim = glm::vec2(25.0f/* * aspectRatio*/, 25.0f);
		//shaderData.projection = glm::ortho(-screenDim.x, screenDim.x, -screenDim.x, screenDim.x);

		title = "Bindless Survivors";

		paused = true;
	}

	~Application() {		
		vkDeviceWaitIdle(VulkanContext::device->logicalDevice);
		for (FrameObjects& frame : frameObjects) {
			destroyBaseFrameObjects(frame);
			delete frame.uniformBuffer;
			delete frame.instanceBuffer;
			delete[] frame.instances;
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

	void loadTexture(const std::string filename)
	{
		uint32_t index;
		loadTexture(filename, index);
	}

	void loadAssets() {		
		game.monsterTypes.loadFromFile(getAssetPath() + "game/monsters.json");
		// @todo
		for (auto& set : game.monsterTypes.sets) {
			for (auto& type : set.types) {
				loadTexture(getAssetPath() + "game/monsters/" + type.image, type.imageIndex);
			}
		}

		// Numbers
		game.firstNumberImageIndex = static_cast<uint32_t>(textures.size());
		for (uint32_t i = 0; i < 10; i++) {
			loadTexture(getAssetPath() + "game/numbers/num_" + std::to_string(i) + ".png");
		}

		// @todo: Player images
		loadTexture(getAssetPath() + "game/players/human_male.png", game.player.imageIndex);

		// @todo: Projectile images
		loadTexture(getAssetPath() + "game/projectiles/magic_bolt_1.png", game.projectileImageIndex);
		loadTexture(getAssetPath() + "game/pickups/misc_crystal_old.png", game.experienceImageIndex);

		// @todo: tile map
		uint32_t dummyIdx;
		loadTexture(getAssetPath() + "game/tiles/set0/grass_0_new.png", tileMap.firstTileIndex);
		loadTexture(getAssetPath() + "game/tiles/set0/grass0-dirt-mix_1.png", tileMap.lastTileIndex);
		loadTexture(getAssetPath() + "game/tiles/set0/grass_full_old.png", tileMap.lastTileIndex);

		SamplerCreateInfo samplerCI {
			.name = "Sprite sampler",
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		};
		spriteSampler = new Sampler(samplerCI);

		samplerCI = {
			.name = "Tile map sampler",
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		};
		tileMap.sampler = new Sampler(samplerCI);

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

	// @todo
	// Tile map for the background is stored as a single one integer channel format, with each pixel storing a zero-based tile index
	void createTileMap () {
		const size_t texBufferSize = tileMap.width * tileMap.height * 4;
		uint32_t* texBuffer = new uint32_t[texBufferSize];
		memset(texBuffer, 0, texBufferSize);
		
		// @todo: random tiles for testing
		std::uniform_int_distribution<uint32_t> rndTile(0, static_cast<uint32_t>(0, tileMap.lastTileIndex - tileMap.firstTileIndex));
		// @todo: generate border to see how tile map size works
		uint32_t tileCounter{ 0 };
		uint32_t tileType = 0;
		for (size_t i = 0; i < tileMap.width * tileMap.height; i++) {
			if (tileCounter >= tileMap.width * 8) {
				tileType = rndTile(game.randomEngine);
				tileCounter = 0;
			}
			//tileType = rndTile(game.randomEngine);
			texBuffer[i] = tileType;
			tileCounter++;
		}

		//for (auto i = 0; i < tileMap.width; i++) {
		//	texBuffer[i] = 2;
		//}
		vks::TextureFromBufferCreateInfo texCI = {
			.buffer = texBuffer,
			.bufferSize = texBufferSize,
			.texWidth = tileMap.width,
			.texHeight = tileMap.height,
			.format = VK_FORMAT_R32_UINT,
			.createSampler = false,
		};
		// @todo: Throws validation errors
		tileMap.texture = new vks::Texture2D(texCI);
		textures.push_back(tileMap.texture);
		tileMap.imageIndex = static_cast<uint32_t>(textures.size() - 1);
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

		tileMap.descriptorSetSampler = new DescriptorSet({
			.pool = descriptorPool,
			.variableDescriptorCount = samplerCount,
			.layouts = { descriptorSetLayoutSamplers->handle },
			.descriptors = {
				{.dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &tileMap.sampler->descriptor},
			}
		});
	}

	void generateQuad()
	{
		std::vector<Vertex> vertices =
		{
			{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f } },
			{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },

			{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
			{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
			{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f } },
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
				instance.pos = glm::vec3(number.position + glm::vec2(i * number.scale * 0.75f, 0.0f), 0.0f);
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

	void prepare() {
		VulkanApplication::prepare();

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
		createTileMap();

		game.player.speed = 5.0f;
		game.player.scale = 1.0f;
		game.player.position = glm::vec2(-(float)tileMap.width / 2.0f, -(float)tileMap.height / 2.0f);


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
			}
		});

		descriptorSetLayoutUniforms = new DescriptorSetLayout({
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }
			}
		});

		for (FrameObjects& frame : frameObjects) {
			frame.descriptorSet = new DescriptorSet({
				.pool = descriptorPool,
				.layouts = { descriptorSetLayoutUniforms->handle },
				.descriptors = {
					{.dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &frame.uniformBuffer->descriptor }
				}
			});
		}
		
		updateTextureDescriptor();

		VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
		pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		pipelineRenderingCreateInfo.colorAttachmentCount = 1;
		pipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChain->colorFormat;
		pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;
		pipelineRenderingCreateInfo.stencilAttachmentFormat = depthFormat;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = 0xf;

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
				{ .location = 4, .binding = 1, .format = VK_FORMAT_R32_SINT, .offset = offsetof(InstanceData, imageIndex) },
				{ .location = 5, .binding = 1, .format = VK_FORMAT_R32_SINT, .offset = offsetof(InstanceData, effect) },
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
			// Index of the tilemap is passed via push constant, tile set starts at that index + 1
			.pushConstantRanges = {
				{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(uint32_t) * 2 + sizeof(float) * 2}
			}
		});

		pipelines["tilemap"] = new Pipeline({
			.shaders = {
				.filename = getAssetPath() + "shaders/tilemap.slang",
				.stages = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT }
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["tilemap"],
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
		pipelineList.push_back(pipelines["tilemap"]);

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
			depthStencil.image,
			0,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 });

		// New structures are used to define the attachments used in dynamic rendering
		colorAttachment = {};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		colorAttachment.imageView = multiSampling ? multisampleTarget.color.view : swapChain->buffers[swapChain->currentImageIndex].view;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (multiSampling) {
			colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			colorAttachment.resolveImageView = swapChain->buffers[swapChain->currentImageIndex].view;
			colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		}

		// A single depth stencil attachment info can be used, but they can also be specified separately.
		// When both are specified separately, the only requirement is that the image view is identical.			
		depthStencilAttachment = {};
		depthStencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		depthStencilAttachment.imageView = multiSampling ? multisampleTarget.depth.view : depthStencil.view;
		depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
		depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthStencilAttachment.clearValue.depthStencil = { 1.0f,  0 };
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

		// Draw tilemap (background)
		struct PushConsts {
			uint32_t uints[2];
			float floats[2];
		} pushConsts;
		pushConsts.uints[0] = tileMap.imageIndex;
		pushConsts.uints[1] = tileMap.firstTileIndex;
		pushConsts.floats[0] = (float)width / 32.0f;
		pushConsts.floats[1] = (float)height / 32.0f;

		pushConsts.floats[0] = 1024.0f / 32.0f;
		pushConsts.floats[1] = 1024.0f / 32.0f;

		cb->bindDescriptorSets(pipelineLayouts["tilemap"], { descriptorSetTextures, tileMap.descriptorSetSampler, frame.descriptorSet });
		cb->bindPipeline(pipelines["tilemap"]);
		cb->updatePushConstant(pipelineLayouts["tilemap"], 0, &pushConsts);
		cb->draw(3, 1, 0, 0);

		// Draw sprites using instancing
		// Instancing buffer stores sprite index, position, scale, direction (to flip/rotate) uv, maybe color for health state

		cb->bindVertexBuffers(0, 1, { quadBuffer->buffer });
		cb->bindVertexBuffers(1, 1, { frame.instanceBuffer->buffer });
		cb->bindDescriptorSets(pipelineLayouts["sprite"], { descriptorSetTextures, descriptorSetSamplers, frame.descriptorSet });
		cb->bindPipeline(pipelines["sprite"]);
		cb->draw(6, frame.instanceBufferDrawCount, 0, 0);
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
			}
		}
		{
			ZoneScopedN("Instance buffer update");
			updateInstanceBuffer(currentFrame);
		}

		shaderData.timer = timer;
		//shaderData.view = glm::mat4(1.0f);
		shaderData.mvp = glm::translate(glm::mat4(1.0f), -glm::vec3(game.player.position / screenDim, 0.0f));
		shaderData.mvp *= glm::ortho(-screenDim.x, screenDim.x, -screenDim.x, screenDim.x);
		shaderData.tileMapSpeed = tileMapSpeed;
		memcpy(currentFrame.uniformBuffer->mapped, &shaderData, sizeof(ShaderData)); // @todo: buffer function

		recordCommandBuffer(currentFrame);
		VulkanApplication::submitFrame(currentFrame);

		for (auto& pipeline : pipelineList) {
			if (pipeline->wantsReload) {
				pipeline->reload();
			}
		}
	}

	void OnUpdateOverlay(vks::UIOverlay& overlay) {
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(0, 90), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Performance");
		ImGui::TextUnformatted(vulkanDevice->properties.deviceName);
		ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / lastFPS), lastFPS);
		ImGui::End();

		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(0, 50), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Player");
		ImGui::Text("XP: %.2f", game.player.experience);
		ImGui::Text("Level: %d", game.player.level);
		ImGui::End();

		ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(0, 50), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Statistics", 0, ImGuiWindowFlags_None);
		ImGui::Text("Monsters: %d", static_cast<uint32_t>(game.monsters.size()));
		ImGui::Text("Projectiles: %d", static_cast<uint32_t>(game.projectiles.size()));
		ImGui::Text("Pickups: %d", static_cast<uint32_t>(game.pickups.size()));
		ImGui::Text("Numbers: %d", static_cast<uint32_t>(game.numbers.size()));
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