idf_component_register(
    SRCS
        "src/vl53l0x_domain.cpp"
        "src/vl53l0x_array.cpp"
    INCLUDE_DIRS "include"
    REQUIRES interfaces domain
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
    )
elseif(MSVC)
    target_compile_options(${COMPONENT_LIB} PRIVATE /W4 /WX)
endif()
