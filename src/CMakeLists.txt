#cmake_minimum_required(VERSION 3.16)
#project(Section_control)

#set(CMAKE_CXX_STANDARD 14)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)

 file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

 set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/components
    )
  
idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    ${CMAKE_SOURCE_DIR}/components
    REQUIRES pca9685
    )
  target_add_binary_data(${COMPONENT_TARGET} "object_pool/object_pool.iop" BINARY)