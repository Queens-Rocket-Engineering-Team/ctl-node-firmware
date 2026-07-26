set(CONFIG_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/esp_config_generate.py)
set(CONFIG_SENSOR_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/sensor_templates.json)
set(CONFIG_CONTROL_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/control_templates.json)

set(CONFIG_HEADER ${CMAKE_CURRENT_BINARY_DIR}/config_json.h)
set(CONFIG_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/config_json.c)

idf_component_register(
    SRCS "${CONFIG_SOURCE}"
    INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES
    ads112c04
    sensors
    controls
)

idf_build_get_property(python PYTHON)
# convert JSON config file to header and source code
add_custom_command(
    OUTPUT "${CONFIG_HEADER}" "${CONFIG_SOURCE}"
    COMMAND ${python} "${CONFIG_SCRIPT}"
            "${BOARD_CONFIG}"
            "${BOARD_MAPPING}"
            "${CONFIG_HEADER}"
            "${CONFIG_SOURCE}"
    DEPENDS "${CONFIG}"
            "${MAPPING}"
            "${CONFIG_SCRIPT}"
            "${CONFIG_SENSOR_TEMPLATE}"
            "${CONFIG_CONTROL_TEMPLATE}"
    VERBATIM
    COMMENT "Generating config_json component"
)

set_source_files_properties("${CONFIG_SOURCE}" PROPERTIES GENERATED TRUE)