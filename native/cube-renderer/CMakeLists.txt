cmake_minimum_required(VERSION 3.31)
project(framebridge-native-cube LANGUAGES CXX)

if(NOT DAWN_ROOT)
  message(FATAL_ERROR "Pass -DDAWN_ROOT=<Dawn checkout>")
endif()
file(TO_CMAKE_PATH "${DAWN_ROOT}" DAWN_ROOT_CMAKE)
add_executable(framebridge-native-cube main.cpp)
target_compile_features(framebridge-native-cube PRIVATE cxx_std_20)
target_include_directories(framebridge-native-cube PRIVATE
  "${DAWN_ROOT_CMAKE}/include"
  "${DAWN_ROOT_CMAKE}/out/debug/gen/include")
file(GLOB_RECURSE DAWN_STATIC_LIBS CONFIGURE_DEPENDS "${DAWN_ROOT_CMAKE}/out/debug/*.lib")
target_link_libraries(framebridge-native-cube PRIVATE ${DAWN_STATIC_LIBS} d3d12 dxgi dxguid OneCore)
if(MSVC)
  target_compile_options(framebridge-native-cube PRIVATE /W4 /permissive-)
endif()
