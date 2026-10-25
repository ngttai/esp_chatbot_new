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

# Host backends are explicit and deterministic by default. `auto` and the real
# backend names are opt-in so a normal build never touches host Wi-Fi, power, or
# audio devices unexpectedly.
set(HOST_SIM_MEDIA_BACKEND "stub" CACHE STRING
    "Host media backend: stub, auto, or ffmpeg_portaudio")
set_property(CACHE HOST_SIM_MEDIA_BACKEND PROPERTY STRINGS stub auto ffmpeg_portaudio)
if(NOT HOST_SIM_MEDIA_BACKEND MATCHES "^(stub|auto|ffmpeg_portaudio)$")
    message(FATAL_ERROR "Invalid HOST_SIM_MEDIA_BACKEND='${HOST_SIM_MEDIA_BACKEND}'")
endif()

set(HOST_SIM_WIFI_BACKEND "stub" CACHE STRING
    "Host Wi-Fi backend: stub, auto, or networkmanager")
set_property(CACHE HOST_SIM_WIFI_BACKEND PROPERTY STRINGS stub auto networkmanager)
if(NOT HOST_SIM_WIFI_BACKEND MATCHES "^(stub|auto|networkmanager)$")
    message(FATAL_ERROR "Invalid HOST_SIM_WIFI_BACKEND='${HOST_SIM_WIFI_BACKEND}'")
endif()

set(HOST_SIM_POWER_BACKEND "stub" CACHE STRING
    "Host power backend: stub, auto, or upower")
set_property(CACHE HOST_SIM_POWER_BACKEND PROPERTY STRINGS stub auto upower)
if(NOT HOST_SIM_POWER_BACKEND MATCHES "^(stub|auto|upower)$")
    message(FATAL_ERROR "Invalid HOST_SIM_POWER_BACKEND='${HOST_SIM_POWER_BACKEND}'")
endif()

set(HOST_SIM_TIME_BACKEND "system" CACHE STRING
    "Host clock backend: system or sntp")
set_property(CACHE HOST_SIM_TIME_BACKEND PROPERTY STRINGS system sntp)
if(NOT HOST_SIM_TIME_BACKEND MATCHES "^(system|sntp)$")
    message(FATAL_ERROR "Invalid HOST_SIM_TIME_BACKEND='${HOST_SIM_TIME_BACKEND}'")
endif()

set(HOST_SIM_WEATHER_BACKEND "mock" CACHE STRING
    "Host weather backend: mock or openweathermap")
set_property(CACHE HOST_SIM_WEATHER_BACKEND PROPERTY STRINGS mock openweathermap)
if(NOT HOST_SIM_WEATHER_BACKEND MATCHES "^(mock|openweathermap)$")
    message(FATAL_ERROR "Invalid HOST_SIM_WEATHER_BACKEND='${HOST_SIM_WEATHER_BACKEND}'")
endif()

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

# Display is always SDL2. Media, Wi-Fi, and power follow the host-facing cache
# options above. Video and BLE are deliberately out of this simulator's scope.
set(BROOKESIA_HAL_LINUX_MEDIA_BACKEND "${HOST_SIM_MEDIA_BACKEND}" CACHE STRING "" FORCE)
set(BROOKESIA_HAL_LINUX_VIDEO_BACKEND stub CACHE STRING "" FORCE)
# Wi-Fi can change the host's active connection. Keep `auto` deterministic and
# require the explicit `networkmanager` value before Brookesia may use it.
if(HOST_SIM_WIFI_BACKEND STREQUAL "networkmanager")
    set(BROOKESIA_HAL_LINUX_WIFI_BACKEND networkmanager CACHE STRING "" FORCE)
else()
    set(BROOKESIA_HAL_LINUX_WIFI_BACKEND stub CACHE STRING "" FORCE)
endif()
set(BROOKESIA_HAL_LINUX_BLE_BACKEND stub CACHE STRING "" FORCE)
set(BROOKESIA_HAL_LINUX_POWER_BACKEND "${HOST_SIM_POWER_BACKEND}" CACHE STRING "" FORCE)
set(BROOKESIA_HAL_LINUX_DISPLAY_BACKEND sdl2 CACHE STRING "" FORCE)
# Exercise the firmware's local Display implementation rather than the upstream copy.
if(NOT TARGET brookesia::hal_interface)
    add_subdirectory(
        "${BROOKESIA_SOURCE_DIR}/hal/brookesia_hal_interface"
        "${CMAKE_BINARY_DIR}/brookesia_hal_interface"
    )
endif()
if(NOT TARGET brookesia::service_storage)
    add_subdirectory(
        "${BROOKESIA_SOURCE_DIR}/service/system/brookesia_service_storage"
        "${CMAKE_BINARY_DIR}/brookesia_service_storage"
    )
endif()
add_subdirectory(
    "${PROJECT_ROOT}/components/brookesia_service_display"
    "${CMAKE_BINARY_DIR}/brookesia_service_display"
)
add_subdirectory(
    "${BROOKESIA_SOURCE_DIR}/hal/brookesia_hal_linux"
    "${CMAKE_BINARY_DIR}/brookesia_hal_linux"
)
add_subdirectory(
    "${PROJECT_ROOT}/components/brookesia_gui_lvgl"
    "${CMAKE_BINARY_DIR}/brookesia_gui_lvgl"
)
add_subdirectory(
    "${BROOKESIA_SOURCE_DIR}/service/media/brookesia_service_audio"
    "${CMAKE_BINARY_DIR}/brookesia_service_audio"
)

set(SPEAKER_UI_DIR "${PROJECT_ROOT}/main/modules/display/speaker_ui")
file(GLOB_RECURSE SPEAKER_UI_SOURCES CONFIGURE_DEPENDS
    "${SPEAKER_UI_DIR}/src/*.c"
    "${SPEAKER_UI_DIR}/vendor/*.c"
)
# The imported source remains byte-identical, but the host target supplies the
# same weather_client_get() ABI so it can select mock or opt-in HTTP data.
list(FILTER SPEAKER_UI_SOURCES EXCLUDE REGEX "/src/flip_clock/weather_source\\.c$")
add_executable(esp_chatbot_host_sim
    brightness_adapter.cpp
    host_capabilities.cpp
    keyboard_adapter.cpp
    main.cpp
    music_fixture.cpp
    persistence_adapter.cpp
    power_adapter.cpp
    volume_adapter.cpp
    weather_adapter.cpp
    wifi_adapter.cpp
    "${PROJECT_ROOT}/main/modules/music/id3_parser.cpp"
    "${PROJECT_ROOT}/main/modules/music/music_data.cpp"
    "${PROJECT_ROOT}/main/modules/music/music_library.cpp"
    "${PROJECT_ROOT}/main/modules/music/music_service.cpp"
    tests/input_injector.cpp
    tests/screenshot_capture.cpp
    tests/self_test_main.cpp
    ${SPEAKER_UI_SOURCES}
)
set(HOST_SIM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HOST_SIM_GENERATED_DIR}")
find_program(HOST_SIM_NMCLI_EXECUTABLE nmcli)
if((HOST_SIM_MEDIA_BACKEND STREQUAL "auto" OR
    HOST_SIM_MEDIA_BACKEND STREQUAL "ffmpeg_portaudio") AND
   BROOKESIA_HAL_LINUX_FFMPEG_PORTAUDIO_FOUND)
    set(HOST_SIM_MEDIA_BACKEND_FFMPEG_PORTAUDIO_ENABLED 1)
    set(HOST_SIM_MEDIA_BACKEND_RESOLVED "ffmpeg_portaudio")
else()
    set(HOST_SIM_MEDIA_BACKEND_FFMPEG_PORTAUDIO_ENABLED 0)
    set(HOST_SIM_MEDIA_BACKEND_RESOLVED "stub")
endif()
if(HOST_SIM_WIFI_BACKEND STREQUAL "networkmanager" AND
   BROOKESIA_HAL_LINUX_NETWORKMANAGER_FOUND AND
   HOST_SIM_NMCLI_EXECUTABLE)
    set(HOST_SIM_WIFI_BACKEND_NETWORKMANAGER_ENABLED 1)
    set(HOST_SIM_WIFI_BACKEND_RESOLVED "networkmanager")
else()
    set(HOST_SIM_WIFI_BACKEND_NETWORKMANAGER_ENABLED 0)
    set(HOST_SIM_WIFI_BACKEND_RESOLVED "stub")
endif()
configure_file(
    "${HOST_SIM_DIR}/host_capabilities_config.hpp.in"
    "${HOST_SIM_GENERATED_DIR}/host_capabilities_config.hpp"
    @ONLY
)
target_include_directories(esp_chatbot_host_sim PRIVATE
    "${HOST_SIM_GENERATED_DIR}"
    "${HOST_SIM_DIR}/tests"
    "${PROJECT_ROOT}/main"
    "${PROJECT_ROOT}/main/modules"
    "${SPEAKER_UI_DIR}/src"
    "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/core/brookesia_core/systems"
    "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/core/brookesia_core/systems/speaker"
    "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/core/brookesia_core/systems/speaker/widgets/quick_settings/ui"
    "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/apps/brookesia_app_ai_profile/ui"
)
find_package(Threads REQUIRED)
target_link_libraries(esp_chatbot_host_sim PRIVATE
    Threads::Threads
    brookesia::gui_lvgl
    brookesia::hal_linux
    brookesia::service_display
    brookesia::service_audio
    brookesia::service_helper
    brookesia::service_manager
    m
)

include(CTest)
if(BUILD_TESTING)
    add_executable(music_library_test
        "${HOST_SIM_DIR}/tests/music_library_test.cpp"
        "${PROJECT_ROOT}/main/modules/music/id3_parser.cpp"
        "${PROJECT_ROOT}/main/modules/music/music_library.cpp")
    target_include_directories(music_library_test PRIVATE "${PROJECT_ROOT}/main")
    add_test(NAME music_library COMMAND music_library_test)
    add_executable(music_service_test
        "${HOST_SIM_DIR}/tests/music_service_test.cpp"
        "${PROJECT_ROOT}/main/modules/music/id3_parser.cpp"
        "${PROJECT_ROOT}/main/modules/music/music_library.cpp"
        "${PROJECT_ROOT}/main/modules/music/music_service.cpp")
    target_include_directories(music_service_test PRIVATE "${PROJECT_ROOT}/main")
    target_link_libraries(music_service_test PRIVATE Threads::Threads)
    add_test(NAME music_service COMMAND music_service_test)
    set_tests_properties(music_service PROPERTIES TIMEOUT 60)
    add_executable(music_data_cover_test
        "${HOST_SIM_DIR}/tests/music_data_cover_test.cpp"
        "${PROJECT_ROOT}/main/modules/music/id3_parser.cpp"
        "${PROJECT_ROOT}/main/modules/music/music_data.cpp"
        "${PROJECT_ROOT}/main/modules/music/music_library.cpp"
        "${PROJECT_ROOT}/main/modules/music/music_service.cpp")
    target_include_directories(music_data_cover_test PRIVATE "${PROJECT_ROOT}/main")
    target_link_libraries(music_data_cover_test PRIVATE Threads::Threads)
    add_test(NAME music_data_cover COMMAND music_data_cover_test)
    set_tests_properties(music_data_cover PROPERTIES TIMEOUT 30)
    add_executable(panel_power_test "${HOST_SIM_DIR}/tests/panel_power_test.cpp")
    target_include_directories(panel_power_test PRIVATE "${PROJECT_ROOT}/main")
    add_test(NAME panel_power COMMAND panel_power_test)
    add_executable(render_suspend_test "${HOST_SIM_DIR}/tests/render_suspend_test.cpp")
    target_include_directories(render_suspend_test PRIVATE "${PROJECT_ROOT}/main")
    add_test(NAME render_suspend COMMAND render_suspend_test)
    add_executable(emote_render_owner_test "${HOST_SIM_DIR}/tests/emote_render_owner_test.cpp")
    target_include_directories(emote_render_owner_test PRIVATE "${PROJECT_ROOT}/main")
    add_test(NAME emote_render_owner COMMAND emote_render_owner_test)
    add_executable(lvgl_render_pause_test "${HOST_SIM_DIR}/tests/lvgl_render_pause_test.cpp"
        "${SPEAKER_UI_DIR}/vendor/esp_speaker_ui/core/brookesia_core/systems/base/assets/fonts/maison_neue_book/esp_brookesia_font_maison_neue_book_14.c")
    target_link_libraries(lvgl_render_pause_test PRIVATE brookesia::gui_lvgl brookesia::hal_linux)
    add_test(NAME lvgl_render_pause COMMAND lvgl_render_pause_test)
    set_tests_properties(lvgl_render_pause PROPERTIES TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_executable(display_admission_test "${HOST_SIM_DIR}/tests/display_admission_test.cpp")
    target_link_libraries(display_admission_test PRIVATE brookesia::service_display brookesia::service_manager brookesia::hal_linux)
    add_test(NAME display_admission COMMAND display_admission_test)
    set_tests_properties(display_admission PROPERTIES TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_executable(power_service_test
        "${HOST_SIM_DIR}/tests/power_service_test.cpp"
        "${PROJECT_ROOT}/main/modules/power_manager.cpp")
    target_include_directories(power_service_test PRIVATE "${PROJECT_ROOT}/main")
    target_compile_definitions(power_service_test PRIVATE
        CONFIG_EXAMPLE_PWR_DIM_TIMEOUT_MS=1000
        CONFIG_EXAMPLE_PWR_HW_SLEEP_TIMEOUT_MS=500
        CONFIG_EXAMPLE_PWR_PREPARE_TIMEOUT_MS=2000
        CONFIG_EXAMPLE_PWR_RESUME_TIMEOUT_MS=2000
        CONFIG_EXAMPLE_PWR_SUSPEND_RETRY_MS=5000
        CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT=10)
    target_link_libraries(power_service_test PRIVATE brookesia::service_manager brookesia::lib_utils)
    add_test(NAME power_service_lifecycle COMMAND power_service_test)
    set_tests_properties(power_service_lifecycle PROPERTIES TIMEOUT 30)
    add_executable(suspend_coordinator_test "${HOST_SIM_DIR}/tests/suspend_coordinator_test.cpp")
    target_include_directories(suspend_coordinator_test PRIVATE "${PROJECT_ROOT}/main/modules")
    add_test(NAME suspend_coordinator COMMAND suspend_coordinator_test)
    add_executable(power_suspend_service_test
        "${HOST_SIM_DIR}/tests/power_suspend_service_test.cpp"
        "${PROJECT_ROOT}/main/modules/power_manager.cpp")
    target_include_directories(power_suspend_service_test PRIVATE "${PROJECT_ROOT}/main")
    target_compile_definitions(power_suspend_service_test PRIVATE
        CONFIG_EXAMPLE_PWR_DIM_TIMEOUT_MS=100
        CONFIG_EXAMPLE_PWR_HW_SLEEP_TIMEOUT_MS=100
        CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT=10
        CONFIG_EXAMPLE_PWR_PREPARE_TIMEOUT_MS=500
        CONFIG_EXAMPLE_PWR_RESUME_TIMEOUT_MS=500
        CONFIG_EXAMPLE_PWR_SUSPEND_RETRY_MS=500)
    target_link_libraries(power_suspend_service_test PRIVATE brookesia::service_manager brookesia::lib_utils)
    add_test(NAME power_suspend_service COMMAND power_suspend_service_test)
    set_tests_properties(power_suspend_service PROPERTIES TIMEOUT 30)
    add_test(NAME host_sim_capabilities COMMAND esp_chatbot_host_sim --print-capabilities)

    set(SPEAKER_UI_SELF_TEST_OPTIONS
        --self-test-home
        --self-test-launcher
        --self-test-2048
        --self-test-music
        --self-test-idle
        --self-test-ui-observability
        --self-test-idle-coordinator
        --self-test-power-policy
        --self-test-lift-to-wake
        --self-test-settings-bar
        --self-test-settings-no-scroll
        --self-test-settings-pages
        --self-test-home-scroll-reset
        --self-test-wifi-control-state
        --self-test-hardware-test-ui
        --self-test-quick-gesture
        --self-test-quick-close-gesture
        --self-test-wlan-toggle
        --self-test-wlan-keyboard
        --self-test-quick-buttons
        --self-test-brightness-simulation
        --self-test-volume-simulation
        --self-test-audio-stub
        --self-test-power-simulation
        --self-test-clock-weather
        --self-test-persistence-write
        --self-test-persistence-read
        --self-test-factory-reset
        --self-test-persistence-defaults
    )
    if(NOT HOST_SIM_WIFI_BACKEND_NETWORKMANAGER_ENABLED)
        list(APPEND SPEAKER_UI_SELF_TEST_OPTIONS --self-test-wifi-mock)
    endif()
    if(HOST_SIM_MEDIA_BACKEND_FFMPEG_PORTAUDIO_ENABLED)
        list(REMOVE_ITEM SPEAKER_UI_SELF_TEST_OPTIONS --self-test-audio-stub)
    endif()
    if(NOT HOST_SIM_WEATHER_BACKEND STREQUAL "mock")
        list(REMOVE_ITEM SPEAKER_UI_SELF_TEST_OPTIONS --self-test-clock-weather)
    endif()
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

    set(PERSISTENCE_TEST_KV_DIR "${CMAKE_BINARY_DIR}/persistence-test-kv")
    set(PERSISTENCE_TEST_FS_DIR "${CMAKE_BINARY_DIR}/persistence-test-fs")
    set_tests_properties(
        speaker_ui_persistence-write
        speaker_ui_persistence-read
        speaker_ui_factory-reset
        speaker_ui_persistence-defaults
        PROPERTIES ENVIRONMENT
            "SDL_VIDEODRIVER=dummy;BROOKESIA_HAL_LINUX_KV_DIR=${PERSISTENCE_TEST_KV_DIR};BROOKESIA_HAL_LINUX_FS_ROOT=${PERSISTENCE_TEST_FS_DIR}"
    )
    set_tests_properties(speaker_ui_persistence-read PROPERTIES
        DEPENDS speaker_ui_persistence-write)
    set_tests_properties(speaker_ui_factory-reset PROPERTIES
        DEPENDS speaker_ui_persistence-read)
    set_tests_properties(speaker_ui_persistence-defaults PROPERTIES
        DEPENDS speaker_ui_factory-reset)
endif()
