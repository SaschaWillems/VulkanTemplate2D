/*
 * Slang Compiler abstraction class
 *
 * Copyright (C) 2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <array>
#include "volk.h"
#include "slang/slang.h"
#include "slang/slang-com-ptr.h"
#include "VulkanContext.h"

class SlangCompiler {
public:
	Slang::ComPtr<slang::IGlobalSession> globalSession;
	Slang::ComPtr<slang::ISession> createSession();
	SlangCompiler();
};

extern SlangCompiler* slangCompiler;