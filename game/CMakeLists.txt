file(GLOB_RECURSE GAME_BASE_SRC_HEADERS "*.hpp")
file(GLOB_RECURSE GAME_BASE_SRC_FILES "*.cpp")

file(GLOB_RECURSE ENTITIY_SRC_HEADERS "entities/*.hpp")
file(GLOB_RECURSE ENTITIY_SRC_FILES "entities/*.cpp")

file(GLOB_RECURSE OBJECT_TYPES_SRC_HEADERS "object_types/*.hpp")
file(GLOB_RECURSE OBJECT_TYPES_SRC_FILES "object_types/*.cpp")

source_group("game" FILES ${GAME_BASE_SRC_FILES})
source_group("entities" FILES ${ENTITIY_SRC_FILES})
source_group("object_types" FILES ${OBJECT_TYPES_SRC_FILES})

add_library(game STATIC ${GAME_BASE_SRC_FILES} ${ENTITIY_SRC_FILES} ${OBJECT_TYPES_SRC_FILES})
