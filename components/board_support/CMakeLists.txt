# Compile-time board profile and pin-policy validation. It deliberately does not
# claim that the unidentified physical board has been verified.
idf_component_register(INCLUDE_DIRS "include")

target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_17)

