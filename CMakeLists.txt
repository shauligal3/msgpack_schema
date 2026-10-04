cmake_minimum_required(VERSION 3.16)

project(mpschema
    VERSION 2.0.0
    DESCRIPTION "Schema-driven MessagePack encoding of C structs"
    LANGUAGES C)

option(MPSCHEMA_BUILD_TESTS    "Build the unit tests"                       ON)
option(MPSCHEMA_BUILD_EXAMPLES "Build the examples"                         ON)
option(MPSCHEMA_WERROR         "Treat compiler warnings as errors"          OFF)
option(MPSCHEMA_SANITIZE       "Build with AddressSanitizer and UBSan"      OFF)

find_package(msgpack-c REQUIRED)

# The schema macros statically initialise a flexible array member, which is a
# GNU extension supported by GCC and Clang.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(mpschema
    src/buffer.c
    src/codec.c
    src/decode.c
    src/encode.c
    src/env.c
    src/header.c
    src/inspect.c
    src/log.c
    src/scan.c
    src/schema.c
    src/transport.c)
add_library(mpschema::mpschema ALIAS mpschema)

target_include_directories(mpschema
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mpschema PUBLIC msgpack-c m)
set_target_properties(mpschema PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# Warnings for the library and everything built in this tree.
set(MPSCHEMA_WARNINGS
    -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes
    -Wpointer-arith -Wcast-qual -Wformat=2 -Wundef -Wvla -Wwrite-strings)
if(MPSCHEMA_WERROR)
    list(APPEND MPSCHEMA_WARNINGS -Werror)
endif()
target_compile_options(mpschema PRIVATE ${MPSCHEMA_WARNINGS})

if(MPSCHEMA_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
    target_compile_options(mpschema PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(mpschema PUBLIC -fsanitize=address,undefined)
endif()

if(MPSCHEMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MPSCHEMA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

include(GNUInstallDirs)
install(TARGETS mpschema EXPORT mpschema-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/mpschema DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT mpschema-targets
    NAMESPACE mpschema::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mpschema)
