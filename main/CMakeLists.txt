set(SRCS
    "main.cpp"
    "modules/ai_agents.cpp"
    "modules/battery_monitor.cpp"
    "modules/developer_mode.cpp"
    "modules/display/display.cpp"
    "modules/display/render_suspend_service.cpp"
    "modules/display/screens/speaker_shell.cpp"
    "modules/general_services.cpp"
    "modules/head_led.cpp"
    "modules/imu_gesture.cpp"
    "modules/mcp_device_tools.cpp"
    "modules/power_manager.cpp"
    "modules/profiler.cpp"
    "modules/touch_sensor.cpp"
    "modules/weather_config.cpp"
    "modules/weather_service.cpp"
    "modules/usb_msc.c"
    "modules/wifi_provisioning.cpp"
    "modules/wifi_control.cpp"
)

if (CONFIG_EXAMPLE_SYSTEM_STATS)
    list(APPEND SRCS "modules/system_stats/system_stats_core.cpp")
endif()

# Limit imported source discovery to the active Speaker UI tree. This
# prevents archived or experimental files elsewhere under main/ from silently
# becoming part of the firmware.
file(GLOB_RECURSE SPEAKER_UI_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/modules/display/speaker_ui/src/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/modules/display/speaker_ui/vendor/*.c"
)
list(APPEND SRCS ${SPEAKER_UI_SRCS})
set(EMBEDDED_FILES "")

if (CONFIG_EXAMPLE_AGENTS_COZE_PRIVATE_KEY_FILE_PATH)
    if (NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/${CONFIG_EXAMPLE_AGENTS_COZE_PRIVATE_KEY_FILE_PATH})
        message(FATAL_ERROR "Coze Private Key file not found: ${CONFIG_EXAMPLE_AGENTS_COZE_PRIVATE_KEY_FILE_PATH}")
    endif()
    list(APPEND EMBEDDED_FILES ${CONFIG_EXAMPLE_AGENTS_COZE_PRIVATE_KEY_FILE_PATH})
    message(STATUS "Coze Private Key Path: ${CONFIG_EXAMPLE_AGENTS_COZE_PRIVATE_KEY_FILE_PATH}")
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS
        "."
        "compat"
        "modules/display/speaker_ui/src"
        "modules/display/speaker_ui/vendor/esp_speaker_ui/core/brookesia_core/systems"
        "modules/display/speaker_ui/vendor/esp_speaker_ui/core/brookesia_core/systems/speaker"
        "modules/display/speaker_ui/vendor/esp_speaker_ui/core/brookesia_core/systems/speaker/widgets/quick_settings/ui"
        "modules/display/speaker_ui/vendor/esp_speaker_ui/apps/brookesia_app_ai_profile/ui"
    PRIV_REQUIRES
        bq27220
        esp_board_manager
        esp_app_format
        esp_driver_usb_serial_jtag
        esp_event
        esp_hw_support
        esp_lvgl_adapter
        esp_wifi
        espressif__i2c_bus
        fatfs
        nvs_flash
        spi_flash
    EMBED_TXTFILES ${EMBEDDED_FILES}
)

if (CONFIG_EXAMPLE_SYSTEM_STATS)
    # Source identity is read when CMake configures; the log reports it as such.
    set(SYSTEM_STATS_GIT_REV "N/A")
    set(SYSTEM_STATS_GIT_DIRTY "N/A")
    find_package(Git QUIET)
    if (GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            OUTPUT_VARIABLE SYSTEM_STATS_REV_OUT
            RESULT_VARIABLE SYSTEM_STATS_REV_RESULT
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if (SYSTEM_STATS_REV_RESULT EQUAL 0 AND SYSTEM_STATS_REV_OUT)
            set(SYSTEM_STATS_GIT_REV "${SYSTEM_STATS_REV_OUT}")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} status --porcelain
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE SYSTEM_STATS_DIRTY_OUT
                RESULT_VARIABLE SYSTEM_STATS_DIRTY_RESULT
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if (SYSTEM_STATS_DIRTY_RESULT EQUAL 0)
                if (SYSTEM_STATS_DIRTY_OUT)
                    set(SYSTEM_STATS_GIT_DIRTY "Y")
                else()
                    set(SYSTEM_STATS_GIT_DIRTY "N")
                endif()
            endif()
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --git-path logs/HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE SYSTEM_STATS_HEAD_LOG
                RESULT_VARIABLE SYSTEM_STATS_HEAD_LOG_RESULT
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if (SYSTEM_STATS_HEAD_LOG_RESULT EQUAL 0 AND SYSTEM_STATS_HEAD_LOG)
                get_filename_component(SYSTEM_STATS_HEAD_LOG_ABS "${SYSTEM_STATS_HEAD_LOG}"
                    ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SYSTEM_STATS_HEAD_LOG_ABS}")
            endif()
        endif()
    endif()
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/modules/profiler.cpp"
        PROPERTIES COMPILE_DEFINITIONS
        "SYSTEM_STATS_GIT_REV=\"${SYSTEM_STATS_GIT_REV}\";SYSTEM_STATS_GIT_DIRTY=\"${SYSTEM_STATS_GIT_DIRTY}\"")
endif()

# Keep imported UI sources byte-identical while accepting their LVGL integer format.
include("${CMAKE_CURRENT_LIST_DIR}/compat/render_power.cmake")

set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/modules/display/speaker_ui/src/speaker_ui.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-error=format"
)

# LVGL and the Emote graphics component both bundle Nayuki qrcodegen with public
# symbols. Prefix LVGL's private copy so the imported SoftAP QR screen can coexist
# with the existing Emote dependency in the same firmware image.
idf_component_get_property(LVGL_COMPONENT_LIB lvgl__lvgl COMPONENT_LIB)
set(LVGL_QRCODEGEN_SYMBOLS
    qrcodegen_calcSegmentBufferSize
    qrcodegen_encodeBinary
    qrcodegen_encodeSegments
    qrcodegen_encodeSegmentsAdvanced
    qrcodegen_encodeText
    qrcodegen_getMinFitVersion
    qrcodegen_getModule
    qrcodegen_getSize
    qrcodegen_isAlphanumeric
    qrcodegen_isNumeric
    qrcodegen_makeAlphanumeric
    qrcodegen_makeBytes
    qrcodegen_makeEci
    qrcodegen_makeNumeric
    qrcodegen_version2size
)
foreach(QRCODEGEN_SYMBOL IN LISTS LVGL_QRCODEGEN_SYMBOLS)
    target_compile_definitions(
        ${LVGL_COMPONENT_LIB} PRIVATE
        "${QRCODEGEN_SYMBOL}=lvgl_${QRCODEGEN_SYMBOL}"
    )
endforeach()

littlefs_create_partition_image(littlefs_data ../littlefs FLASH_IN_PROJECT)

set(ASSETS_PARTITION_NAME "anim_icon")
set(ASSETS_BIN_NAME ${ASSETS_PARTITION_NAME})
set(SUPPORTED_RESOLUTIONS "320_240" "360_360" "1024_600")
set(DEFAULT_RESOLUTION "320_240")
set(RESOLUTION "${CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_H_RES}_${CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_V_RES}")
# Use an exact asset resolution when available. Otherwise, choose the largest supported
# resolution that does not exceed the configured panel size, then fall back to the default.
if (NOT RESOLUTION IN_LIST SUPPORTED_RESOLUTIONS)
    set(SELECTED_RESOLUTION "")
    set(SELECTED_RESOLUTION_SCORE "")
    foreach(candidate_resolution IN LISTS SUPPORTED_RESOLUTIONS)
        string(REPLACE "_" ";" candidate_resolution_parts "${candidate_resolution}")
        list(GET candidate_resolution_parts 0 candidate_h_res)
        list(GET candidate_resolution_parts 1 candidate_v_res)

        if (
            (candidate_h_res LESS_EQUAL CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_H_RES) AND
            (candidate_v_res LESS_EQUAL CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_V_RES)
        )
            math(EXPR candidate_h_delta
                "${CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_H_RES} - ${candidate_h_res}"
            )
            math(EXPR candidate_v_delta
                "${CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_V_RES} - ${candidate_v_res}"
            )
            math(EXPR candidate_score "${candidate_h_delta} + ${candidate_v_delta}")

            if (SELECTED_RESOLUTION STREQUAL "")
                set(SELECTED_RESOLUTION "${candidate_resolution}")
                set(SELECTED_RESOLUTION_SCORE "${candidate_score}")
            elseif (candidate_score LESS SELECTED_RESOLUTION_SCORE)
                set(SELECTED_RESOLUTION "${candidate_resolution}")
                set(SELECTED_RESOLUTION_SCORE "${candidate_score}")
            endif()
        endif()
    endforeach()

    if (SELECTED_RESOLUTION STREQUAL "")
        message(
            WARNING
            "Unsupported asset resolution '${RESOLUTION}', using default '${DEFAULT_RESOLUTION}' "
            "(supported: ${SUPPORTED_RESOLUTIONS})"
        )
        set(RESOLUTION "${DEFAULT_RESOLUTION}")
    else()
        message(
            WARNING
            "Unsupported asset resolution '${RESOLUTION}', using nearest '${SELECTED_RESOLUTION}' "
            "(supported: ${SUPPORTED_RESOLUTIONS})"
        )
        set(RESOLUTION "${SELECTED_RESOLUTION}")
    endif()
endif()
message(STATUS "Asset Resolution: ${RESOLUTION}")
set(ASSETS_FILE "${CMAKE_BINARY_DIR}/${ASSETS_BIN_NAME}.bin")
set(EMOTE_ASSETS_EXTERNAL_PATH "${CMAKE_CURRENT_LIST_DIR}/../assets/emote")
build_speaker_assets_bin(
    ${ASSETS_BIN_NAME}
    ${RESOLUTION}
    ${ASSETS_FILE}
    ${CONFIG_MMAP_FILE_NAME_LENGTH}
    ${EMOTE_ASSETS_EXTERNAL_PATH}
)
message(STATUS "Generated emote assets: ${ASSETS_FILE} -> ${ASSETS_PARTITION_NAME} partition")
esptool_py_flash_to_partition(flash ${ASSETS_PARTITION_NAME} "${ASSETS_FILE}")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ASSETS_PARTITION_NAME="${ASSETS_PARTITION_NAME}"
)

if(DEFINED ENV{IDF_CI_BUILD})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        IDF_CI_BUILD
    )
endif()

#
# The following WiFi credentials are used for CI test. Please ignore them if you are not running CI test.
#
if(DEFINED ENV{CI_TEST_WIFI_2_4G_AP1_SSID})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        CI_TEST_WIFI_2_4G_AP1_SSID="$ENV{CI_TEST_WIFI_2_4G_AP1_SSID}"
    )
endif()
if(DEFINED ENV{CI_TEST_WIFI_2_4G_AP1_PSW})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        CI_TEST_WIFI_2_4G_AP1_PSW="$ENV{CI_TEST_WIFI_2_4G_AP1_PSW}"
    )
endif()
