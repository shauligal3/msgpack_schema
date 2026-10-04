add_executable(telemetry telemetry.c)
target_link_libraries(telemetry PRIVATE mpschema::mpschema)
