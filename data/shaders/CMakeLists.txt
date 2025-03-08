add_custom_target(shaders)
file(GLOB_RECURSE SHADER_FILES "*.slang" "base/*.slang" "includes/*.slang")
source_group("shaders" FILES ${SHADER_FILES})
target_sources(shaders PRIVATE ${SHADER_FILES})
