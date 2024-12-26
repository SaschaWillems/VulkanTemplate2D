file(GLOB_RECURSE GAME_BASE_SRC_HEADERS "*.hpp")
file(GLOB_RECURSE GAME_BASE_SRC_FILES "*.cpp")

file(GLOB_RECURSE ENTITIY_SRC_HEADERS "entities/*.hpp")
file(GLOB_RECURSE ENTITIY_SRC_FILES "entities/*.cpp")

file(GLOB_RECURSE OBJECT_TYPES_SRC_HEADERS "object_types/*.hpp")
file(GLOB_RECURSE OBJECT_TYPES_SRC_FILES "object_types/*.cpp")

source_group("game" FILES ${GAME_BASE_SRC_FILES})
source_group("entities" FILES ${ENTITIY_SRC_FILES})
source_group("object_types" FILES ${OBJECT_TYPES_SRC_FILES})

# SFML
set(SFML_VERSION "2.6.1")

FetchContent_Declare(
    sfml
    GIT_REPOSITORY "https://github.com/SFML/SFML.git"
    GIT_TAG        "${SFML_VERSION}"
)

FetchContent_GetProperties(sfml)
if(NOT sfml_POPULATED)
    FetchContent_Populate(sfml)
endif()
message(STATUS ${sfml_SOURCE_DIR})
include_directories(${sfml_SOURCE_DIR}/include)

add_library(game STATIC ${GAME_BASE_SRC_FILES} ${ENTITIY_SRC_FILES} ${OBJECT_TYPES_SRC_FILES})
