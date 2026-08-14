# Allocation-free task-liveness monitoring shared by application and safety.
idf_component_register(
    SRCS "src/task_health_monitor.cpp"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
)
