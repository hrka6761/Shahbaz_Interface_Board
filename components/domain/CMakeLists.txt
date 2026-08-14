# Dependency-free domain value types. No ESP-IDF or FreeRTOS dependencies.
idf_component_register(INCLUDE_DIRS "include")

target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_17)

