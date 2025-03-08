/*
 * Slang Compiler abstraction class
 *
 * Copyright (C) 2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "slang.hpp"

SlangCompiler* slangCompiler{ nullptr };

Slang::ComPtr<slang::ISession> SlangCompiler::createSession()
{
	Slang::ComPtr<slang::ISession> session;
	auto targets{ std::to_array<slang::TargetDesc>({ {.format{SLANG_SPIRV}, .profile{globalSession->findProfile("spirv_1_6")} } }) };
	auto options{ std::to_array<slang::CompilerOptionEntry>({ { slang::CompilerOptionName::EmitSpirvDirectly, {slang::CompilerOptionValueKind::Int, 1} } }) };
	slang::SessionDesc desc{ .targets{targets.data()}, .targetCount{SlangInt(targets.size())}, .compilerOptionEntries{options.data()}, .compilerOptionEntryCount{uint32_t(options.size())} };
	desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
	SlangResult res = slangCompiler->globalSession->createSession(desc, session.writeRef());
	if (res == SLANG_FAIL) {
		throw std::runtime_error("Could not init Slang library");
	}
	return session;
}

SlangCompiler::SlangCompiler()
{
	slang::createGlobalSession(globalSession.writeRef());
}