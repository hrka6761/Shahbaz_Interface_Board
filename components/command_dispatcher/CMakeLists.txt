idf_component_register(
    SRCS "src/command_dispatcher.cpp"
    INCLUDE_DIRS "include"
    REQUIRES telemetry_protocol safety_supervisor
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
)
