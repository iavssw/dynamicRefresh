cmake_minimum_required(VERSION 3.13.4)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(submodules/intelpcm)

add_subdirectory(src)

#target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/submodules/intelpcm/src)
#add_dependencies(submodules/intelpcm src)



