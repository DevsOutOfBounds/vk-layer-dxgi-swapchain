cmake_minimum_required (VERSION 3.10)

if(NOT WIN32)
  message(FATAL_ERROR "This vulkan layer is only supported on Windows platforms!")
endif()


# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


# TODO: find_package(VulkanUtilityLibraries REQUIRED)
if (DEFINED VK_SDK_PATH)
	set(VK_INCLUDE_DIR "${VK_SDK_PATH}/Include")
	set(VK_LIBRARY_DIR "${VK_SDK_PATH}/Lib")
	set(VK_FOUND "True")
else()
	find_package(Vulkan REQUIRED)
	
	set(VK_INCLUDE_DIR ${Vulkan_INCLUDE_DIRS})
    set(VK_LIB_PATH ${Vulkan_LIBRARIES})
    set(VK_FOUND ${Vulkan_FOUND})
endif()

if (NOT VK_FOUND)
	message(FATAL_ERROR "Could not find Vulkan SDK")
else()
	message(STATUS "Using Vulkan SDK at: ${VK_INCLUDE_DIR}")
endif()

project ("doob-vk-dxgi-swapchain")

# Add source to this project's executable.
add_library (doob-vk-dxgi-swapchain SHARED 
"src/vk/vk_layer_dispatch_table.h"
"src/Core.h" 
"src/Instance.cpp" 

"src/DxgiSwapchain.h" 
"src/DxgiSwapchain.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET doob-vk-dxgi-swapchain PROPERTY CXX_STANDARD 20)
endif()

target_include_directories(doob-vk-dxgi-swapchain PRIVATE
	${VK_INCLUDE_DIR}
)

target_link_libraries(doob-vk-dxgi-swapchain
	PUBLIC
	${VK_LIB_PATH}
)

# TODO: Add tests
