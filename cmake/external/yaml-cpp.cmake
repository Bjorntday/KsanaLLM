# Copyright 2024 Tencent Inc.  All rights reserved.
#
# ==============================================================================

include(FetchContent)

set(YAML_INSTALL_DIR ${THIRD_PARTY_PATH}/install/yaml-cpp)

option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)

if(NOT DEFINED YAML_VER)
    set(YAML_VER 0.7.0)
endif()
set(YAML_GIT_URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-${YAML_VER}.tar.gz)

FetchContent_Declare(
    yaml_cpp
    URL        ${YAML_GIT_URL}
    SOURCE_DIR ${YAML_INSTALL_DIR}
)

FetchContent_GetProperties(yaml_cpp)
if(NOT yaml_cpp_POPULATED)
    FetchContent_Populate(yaml_cpp)

    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(YAML_CPP_BUILD_TESTS OFF)
    set(YAML_CPP_BUILD_TOOLS OFF)
    add_subdirectory(${yaml_cpp_SOURCE_DIR} ${yaml_cpp_BINARY_DIR})
endif()

message(STATUS "Yaml-cpp source directory: ${yaml_cpp_SOURCE_DIR}")
message(STATUS "Yaml-cpp binary directory: ${yaml_cpp_BINARY_DIR}")

include_directories(${yaml_cpp_SOURCE_DIR}/include)

# the declare yaml-cpp will generate libyaml-cpp.a,
# the involked library name is yaml-cpp, we alias it as ksana_llm_yaml
add_library(ksana_llm_yaml ALIAS yaml-cpp)
