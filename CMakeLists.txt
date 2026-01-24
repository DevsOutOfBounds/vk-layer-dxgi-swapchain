cmake_minimum_required (VERSION 3.10)

if(NOT WIN32)
  message(FATAL_ERROR "This vulkan layer is only supported on Windows platforms!")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(DOOB_DXGI_HEADER_PATH "src/generated/include/vk_doob_dxgi.h")
message(STATUS "Generating DOOB headers and manifest...")
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_headers.py --output_header=${DOOB_DXGI_HEADER_PATH}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE script_result
    OUTPUT_VARIABLE script_output
    ERROR_VARIABLE script_error
)


# 3. Check for failure (Stop CMake if the script crashes)
if(NOT script_result EQUAL 0)
    message(FATAL_ERROR "Python script failed:\n${script_output}\n${script_error}")
endif()

project ("doob-vk-dxgi-swapchain")

# Add source to this project's executable.
add_library (doob-vk-dxgi-swapchain SHARED 
"src/Core.h" 
"src/Instance.cpp" 

"src/DxgiSwapchain.h" 
"src/DxgiSwapchain.cpp"
${DOOB_DXGI_HEADER_PATH}
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET doob-vk-dxgi-swapchain PROPERTY CXX_STANDARD 20)
endif()

target_include_directories(doob-vk-dxgi-swapchain PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command(
    TARGET doob-vk-dxgi-swapchain POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/gen_manifest.py --output_json ${CMAKE_CURRENT_BINARY_DIR}/VkLayerDoob_dxgi_swapchain.json --library_name doob-vk-dxgi-swapchain 
    COMMENT "Generating Layer Manifest"
)

# TODO: Add tests
