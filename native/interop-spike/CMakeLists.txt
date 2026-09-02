cmake_minimum_required(VERSION 3.31)
project(framebridge-dawn-interop-spike LANGUAGES CXX)

if(NOT DAWN_ROOT)
  message(FATAL_ERROR "Pass -DDAWN_ROOT=<Dawn checkout>")
endif()

add_executable(framebridge-dawn-interop-spike main.cpp)
target_compile_features(framebridge-dawn-interop-spike PRIVATE cxx_std_20)
target_include_directories(framebridge-dawn-interop-spike PRIVATE
  "${DAWN_ROOT}/include"
  "${DAWN_ROOT}/out/debug/gen/include")
target_link_libraries(framebridge-dawn-interop-spike PRIVATE
  "${DAWN_ROOT}/out/debug/src/dawn/native/dawn_native.lib")
file(TO_CMAKE_PATH "${DAWN_ROOT}" DAWN_ROOT_CMAKE)
file(GLOB_RECURSE DAWN_STATIC_LIBS CONFIGURE_DEPENDS
  "${DAWN_ROOT_CMAKE}/out/debug/*.lib")
target_link_libraries(framebridge-dawn-interop-spike PRIVATE ${DAWN_STATIC_LIBS} d3d12 dxgi dxguid OneCore)
if(MSVC)
  target_compile_options(framebridge-dawn-interop-spike PRIVATE /W4 /permissive-)
endif()
