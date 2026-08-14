idf_component_register(
    SRCS
        "src/cobs.cpp"
        "src/crc32c.cpp"
        "src/frame_accumulator.cpp"
        "src/wire_protocol.cpp"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
)
