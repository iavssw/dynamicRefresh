project(dynamicRefresh)

add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/submodules/intelpcm/src)

















# refs
#target_link_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/lib/)
#target_link_libraries(${PROJECT_NAME} /home/greg/Desktop/dynamicRefreash/build/lib/libpcm.so)
#target_link_libraries(${PROJECT_NAME} libpcm.so)
