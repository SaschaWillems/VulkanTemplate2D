/*
 * Vulkan pipeline abstraction class
 *
 * Copyright (C) 2023-2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <vector>
#include "volk.h"
#include <stdexcept>
#if defined(__ANDROID__)
#include "Android.h"
#endif
#include "DeviceResource.h"
#include "Device.hpp"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "PipelineLayout.hpp"
#include "slang.hpp"

enum class DynamicState { Viewport, Scissor };

struct PipelineVertexInput {
	std::vector<VkVertexInputBindingDescription> bindings{};
	std::vector<VkVertexInputAttributeDescription> attributes{};
};

struct PipelineCreateInfo {
	const std::string name{ "" };
	VkPipelineBindPoint bindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS };
	struct {
		std::string filename;
		std::vector<VkShaderStageFlagBits> stages{};
	} shaders;
	VkPipelineCache cache{ VK_NULL_HANDLE };
	VkPipelineLayout layout;
	VkPipelineCreateFlags flags;
	PipelineVertexInput vertexInput{};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
	VkPipelineTessellationStateCreateInfo tessellationState{};
	VkPipelineViewportStateCreateInfo viewportState{};
	VkPipelineRasterizationStateCreateInfo rasterizationState{};
	VkPipelineMultisampleStateCreateInfo multisampleState{};
	VkPipelineDepthStencilStateCreateInfo depthStencilState{};
	struct {
		std::vector<VkPipelineColorBlendAttachmentState> attachments{};
	} blending;
	std::vector<DynamicState> dynamicState{};
	VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
	bool enableHotReload{ false };
};

class Pipeline : public DeviceResource {
private:
	VkPipeline handle{ VK_NULL_HANDLE };
	
	void createPipelineObject(PipelineCreateInfo createInfo) {
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages{};
		Slang::ComPtr<slang::ISession> session = slangCompiler->createSession();

		// Slang allows for all shader stages to be stored in a single file
		VkShaderModule shaderModule{ VK_NULL_HANDLE };
		try {
			Slang::ComPtr<slang::IModule> slangModule{ session->loadModuleFromSource(createInfo.name.c_str(), createInfo.shaders.filename.c_str(), nullptr, nullptr) };
			Slang::ComPtr<ISlangBlob> spirv;
			slangModule->getTargetCode(0, spirv.writeRef());

			VkShaderModuleCreateInfo shaderModuleCI{};
			shaderModuleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			shaderModuleCI.codeSize = spirv->getBufferSize();
			shaderModuleCI.pCode = (uint32_t*)spirv->getBufferPointer();
			VkShaderModule shaderModule;
			vkCreateShaderModule(VulkanContext::device->logicalDevice, &shaderModuleCI, nullptr, &shaderModule);

			VkPipelineShaderStageCreateInfo shaderStageCI{};
			shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStageCI.module = shaderModule;
			shaderStageCI.pName = "main";
			for (auto& stage : createInfo.shaders.stages) {
				shaderStageCI.stage = stage;
				shaderStages.push_back(shaderStageCI);
			}
		}
		catch (...) {
			throw;
		}

		createInfo.inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		createInfo.viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		createInfo.rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		createInfo.multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		createInfo.depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		createInfo.pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;

		std::vector<VkDynamicState> dstates{};
		for (auto& s : createInfo.dynamicState) {
			switch (s) {
			case DynamicState::Scissor:
				dstates.push_back(VK_DYNAMIC_STATE_SCISSOR);
				break;
			case DynamicState::Viewport:
				dstates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
				break;
			}
		}
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = (uint32_t)dstates.size();
		dynamicState.pDynamicStates = dstates.data();

		VkPipelineVertexInputStateCreateInfo vertexInputState{};
		vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(createInfo.vertexInput.bindings.size());
		vertexInputState.pVertexBindingDescriptions = createInfo.vertexInput.bindings.data();
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(createInfo.vertexInput.attributes.size());
		vertexInputState.pVertexAttributeDescriptions = createInfo.vertexInput.attributes.data();

		VkPipelineColorBlendStateCreateInfo colorBlendState{};
		colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendState.attachmentCount = static_cast<uint32_t>(createInfo.blending.attachments.size());
		colorBlendState.pAttachments = createInfo.blending.attachments.data();

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.layout = createInfo.layout;
		pipelineCI.pVertexInputState = &vertexInputState;
		pipelineCI.pInputAssemblyState = &createInfo.inputAssemblyState;
		pipelineCI.pTessellationState = &createInfo.tessellationState;
		pipelineCI.pViewportState = &createInfo.viewportState;
		pipelineCI.pRasterizationState = &createInfo.rasterizationState;
		pipelineCI.pMultisampleState = &createInfo.multisampleState;
		pipelineCI.pDepthStencilState = &createInfo.depthStencilState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.pNext = &createInfo.pipelineRenderingInfo; // createInfo.pNext;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(VulkanContext::device->logicalDevice, createInfo.cache, 1, &pipelineCI, nullptr, &handle));
	
		vkDestroyShaderModule(VulkanContext::device->logicalDevice, shaderModule, nullptr);

		bindPoint = createInfo.bindPoint;
	}

public:
	// Store the createInfo for hot reload
	PipelineCreateInfo* initialCreateInfo{ nullptr };
	VkPipelineBindPoint bindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS };
	bool wantsReload = false;

	Pipeline(PipelineCreateInfo createInfo) : DeviceResource(createInfo.name) {
		createPipelineObject(createInfo);

		// Store a copy of the createInfo for hot reload		
		if (createInfo.enableHotReload) {
			initialCreateInfo = new PipelineCreateInfo(createInfo);
		}

		setDebugName((uint64_t)handle, VK_OBJECT_TYPE_PIPELINE);
	};

	~Pipeline() {
		vkDestroyPipeline(VulkanContext::device->logicalDevice, handle, nullptr);
	}

	void reload() {
		wantsReload = false;
		assert(initialCreateInfo);
		// @todo: move to calling function to avoid multiple wait idles
		VulkanContext::device->waitIdle();
		// For hot reloads create a temp handle, so if pipeline creation fails the application will continue with the old pipeline
		VkPipeline oldHandle = handle;
		try {
			createPipelineObject(*initialCreateInfo);
			vkDestroyPipeline(VulkanContext::device->logicalDevice, oldHandle, nullptr);
			std::cout << "Pipeline recreated\n";
		} catch (...) {
			std::cerr << "Could not recreate pipeline, using last version\n";
		}
	}

	operator VkPipeline() { return handle; };
};
