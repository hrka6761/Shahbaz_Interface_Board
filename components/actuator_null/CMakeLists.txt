idf_component_register(
    SRCS "src/null_actuator_controller.cpp"
    INCLUDE_DIRS "include"
    REQUIRES safety_supervisor
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
)
