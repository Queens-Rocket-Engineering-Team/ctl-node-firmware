idf_component_register(
    SRCS "sensor.c" "pressure_transducer.c" "thermocouple.c" "load_cell.c" "resistance_sensor.c" "current_sensor.c"
    INCLUDE_DIRS .
    REQUIRES
    ads112c04
)