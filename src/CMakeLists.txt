

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(dynamicRefresh)

# add_executable(${PROJECT_NAME} main.cpp)
#add_executable(${PROJECT_NAME} main_base_err_track_no_temp.cpp)
add_executable(${PROJECT_NAME} main_base_err_track_temp_slope.cpp)


# add_executable(${PROJECT_NAME} main_bw_read_core.cpp)
#add_executable(${PROJECT_NAME} main_BW.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/submodules/intelpcm/src) 
target_link_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/build/lib/)
target_link_libraries(${PROJECT_NAME} libpcm.so)










# refs
#target_link_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/lib/)
#target_link_libraries(${PROJECT_NAME} /home/chihun/ECE511/dynamicRefresh/build/lib/libpcm.so)
#target_link_libraries(${PROJECT_NAME} libpcm.so)
