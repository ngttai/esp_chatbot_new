# Add the ESP-IDF sleep operation without mutating the pinned managed driver.
set(power_generated "${CMAKE_BINARY_DIR}/render_power")
idf_component_get_property(panel_dir espressif__esp_lcd_st77916 COMPONENT_DIR)
idf_component_get_property(panel_lib espressif__esp_lcd_st77916 COMPONENT_LIB)
file(SHA256 "${panel_dir}/esp_lcd_st77916_spi.c" panel_hash)
if(NOT panel_hash STREQUAL "a464c88fcfd485bd418e42cfdd099cede661936ffe81ddcb0f46b3d466cc7f67")
    message(FATAL_ERROR "ST77916 sleep extension requires review for the resolved driver version")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${panel_dir}/esp_lcd_st77916_spi.c")
file(READ "${panel_dir}/esp_lcd_st77916_spi.c" panel_source)
render_power_replace(panel_source
    "static esp_err_t panel_st77916_disp_on_off(esp_lcd_panel_t *panel, bool off);"
    "static esp_err_t panel_st77916_disp_on_off(esp_lcd_panel_t *panel, bool off);\nstatic esp_err_t panel_st77916_disp_sleep(esp_lcd_panel_t *panel, bool sleep);")
render_power_replace(panel_source "    st77916->base.disp_on_off = panel_st77916_disp_on_off;"
    "    st77916->base.disp_on_off = panel_st77916_disp_on_off;\n    st77916->base.disp_sleep = panel_st77916_disp_sleep;")
string(APPEND panel_source "
// Only sends the command; the caller owns nonblocking settle deadlines.
static esp_err_t panel_st77916_disp_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    st77916_panel_t *st77916 = __containerof(panel, st77916_panel_t, base);
    return tx_param(st77916, st77916->io, sleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT, NULL, 0);
}
")
file(CONFIGURE OUTPUT "${power_generated}/st77916_spi.c" CONTENT "${panel_source}" @ONLY)
get_target_property(panel_sources ${panel_lib} SOURCES)
list(FILTER panel_sources EXCLUDE REGEX "esp_lcd_st77916_spi\\.c$")
set_property(TARGET ${panel_lib} PROPERTY SOURCES "${panel_sources}")
target_sources(${panel_lib} PRIVATE "${power_generated}/st77916_spi.c")
