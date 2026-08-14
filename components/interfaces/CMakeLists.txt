# Clean-architecture ports used by application and infrastructure components.
idf_component_register(INCLUDE_DIRS "include" REQUIRES domain)

target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_17)

