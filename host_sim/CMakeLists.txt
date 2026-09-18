cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

project(esp_chatbot_v1_host_sim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HOST_SIM_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PROJECT_ROOT "${HOST_SIM_DIR}/..")

set(
    BROOKESIA_SOURCE_DIR
    "${PROJECT_ROOT}/../esp-brookesia"
    CACHE PATH
    "Path to an ESP-Brookesia v0.8 source checkout"
)
set(
    LVGL_SOURCE_DIR
    "${PROJECT_ROOT}/managed_components/lvgl__lvgl"
    CACHE PATH
    "Path to the LVGL source used by the ESP-IDF project"
)

if(NOT EXISTS "${BROOKESIA_SOURCE_DIR}/hal/brookesia_hal_linux/CMakeLists.txt")
    message(FATAL_ERROR
        "ESP-Brookesia source was not found at '${BROOKESIA_SOURCE_DIR}'. "
        "Pass -DBROOKESIA_SOURCE_DIR=/absolute/path/to/esp-brookesia."
    )
endif()
if(NOT EXISTS "${LVGL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "LVGL source was not found at '${LVGL_SOURCE_DIR}'. Run the ESP-IDF component "
        "resolver once (idf.py reconfigure), or pass "
        "-DLVGL_SOURCE_DIR=/absolute/path/to/lvgl."
    )
endif()

# Brookesia's portable dependency chain. Order matters because the PC CMake files
# expect the lower-level aliases to exist before they are added.
add_subdirectory(
    "${BROOKESIA_SOURCE_DIR}/utils/brookesia_lib_utils"
    "${CMAKE_BINARY_DIR}/brookesia_lib_utils"
)
add_subdirectory(
    "${BROOKESIA_SOURCE_DIR}/service/framework/brookesia_service_manager"
    "${CMAKE_BINARY_DIR}/brookesia_service_manager"
)
add_subdirectory(
    "${BROOKESIA_SOURCE_DIR}/service/framework/brookesia_service_helper"
    "${CMAKE_BINARY_DIR}/brookesia_service_helper"
)
add_subdirectory(
    "${BROOKESIA_SOURCE_DIR}/gui/brookesia_gui_interface"
    "${CMAKE_BINARY_DIR}/brookesia_gui_interface"
)

# Build the same LVGL major version as the firmware, but let Brookesia HAL Linux
# own the SDL window. LVGL's built-in SDL driver stays disabled in lv_conf.h.
set(LV_BUILD_CONF_PATH "${HOST_SIM_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
find_package(SDL2 REQUIRED)
add_subdirectory("${LVGL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/lvgl")

# Some Brookesia sources include LVGL as lvgl/lvgl.h. Component-manager source
# directories are named lvgl__lvgl, so expose a build-local include alias.
set(LVGL_INCLUDE_SHIM_DIR "${CMAKE_BINARY_DIR}/lvgl_include_shim")
file(MAKE_DIRECTORY "${LVGL_INCLUDE_SHIM_DIR}")
if(NOT EXISTS "${LVGL_INCLUDE_SHIM_DIR}/lvgl")
    file(CREATE_LINK "${LVGL_SOURCE_DIR}" "${LVGL_INCLUDE_SHIM_DIR}/lvgl" SYMBOLIC)
endif()
target_include_directories(lvgl PUBLIC "${LVGL_INCLUDE_SHIM_DIR}")
if(SDL2_INCLUDE_DIRS)
    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
endif()
if(TARGET SDL2::SDL2)
    target_link_libraries(lvgl PUBLIC SDL2::SDL2)
else()
    target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

# Only the display needs a real host backend. Other hardware families use
# deterministic stubs so the simulator is usable without a microphone/camera.
set(BROOKESIA_HAL_LINUX_MEDIA_BACKEND stub CACHE STRING "" FORCE)
set(BROOKESIA_HAL_LINUX_VIDEO_BACKEND stub CACHE STRING "" FORCE)
set(BROOKESIA_HAL_LINUX_WIFI_BACKEND stub CACHE STRING "" FORCE)
set(BROOKESIA_HAL_LINUX_BLE_BACKEND stub CACHE STRING "" FORCE)
set(BROOKESIA_HAL_LINUX_POWER_BACKEND stub CACHE STRING "" FORCE)
set(BROOKESIA_HAL_LINUX_DISPLAY_BACKEND sdl2 CACHE STRING "" FORCE)
add_subdirectory(
    "${BROOKESIA_SOURCE_DIR}/gui/brookesia_gui_lvgl"
    "${CMAKE_BINARY_DIR}/brookesia_gui_lvgl"
)

set(SPEAKER_UI_DIR "${PROJECT_ROOT}/main/modules/display/speaker_ui")
file(GLOB_RECURSE SPEAKER_UI_SOURCES CONFIGURE_DEPENDS
    "${SPEAKER_UI_DIR}/src/*.c"
    "${SPEAKER_UI_DIR}/vendor/*.c"
)
add_executable(esp_chatbot_host_sim
    brightness_adapter.cpp
    main.cpp
    tests/input_injector.cpp
    tests/screenshot_capture.cpp
    tests/self_test_main.cpp
    ${SPEAKER_UI_SOURCES}
)
target_include_directories(esp_chatbot_host_sim PRIVATE
    "${HOST_SIM_DIR}/tests"
    "${SPEAKER_UI_DIR}/src"
    "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/main/core/brookesia_core/systems"
    "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/main/core/brookesia_core/systems/speaker"
    "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/main/core/brookesia_core/systems/speaker/widgets/quick_settings/ui"
    "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/main/apps/brookesia_app_ai_profile/ui"
)
target_link_libraries(esp_chatbot_host_sim PRIVATE
    brookesia::gui_lvgl
    brookesia::hal_linux
    brookesia::service_display
    brookesia::service_helper
    brookesia::service_manager
    m
)

include(CTest)
if(BUILD_TESTING)
    set(SPEAKER_UI_SELF_TEST_OPTIONS
        --self-test-home
        --self-test-launcher
        --self-test-idle
        --self-test-settings-bar
        --self-test-settings-no-scroll
        --self-test-settings-pages
        --self-test-quick-gesture
        --self-test-quick-close-gesture
        --self-test-wlan-toggle
        --self-test-wlan-keyboard
        --self-test-quick-buttons
        --self-test-brightness-simulation
    )
    foreach(SELF_TEST_OPTION IN LISTS SPEAKER_UI_SELF_TEST_OPTIONS)
        string(REPLACE "--self-test-" "" SELF_TEST_NAME "${SELF_TEST_OPTION}")
        add_test(
            NAME "speaker_ui_${SELF_TEST_NAME}"
            COMMAND esp_chatbot_host_sim "${SELF_TEST_OPTION}"
        )
        set_tests_properties("speaker_ui_${SELF_TEST_NAME}" PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            RUN_SERIAL TRUE
            TIMEOUT 30
        )
    endforeach()
endif()
