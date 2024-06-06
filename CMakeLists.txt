cmake_minimum_required(VERSION 3.10)
project(bprof)

# Use brew to find PHP include and library directories
set(PHP_PREFIX_DIR /opt/homebrew/Cellar/php/8.3.7)

# Set the PHP include directory and PHP library directory
set(PHP_INCLUDE_DIR ${PHP_PREFIX_DIR}/include/php)
set(PHP_EXTENSION_DIR ${PHP_PREFIX_DIR}/lib/php)

# Find the PHP headers and libraries
include_directories(${PHP_INCLUDE_DIR})

# Add the source files for the extension
set(SOURCE_FILES
        php_bprof.c
        # Add other source files here
)

# Create the shared library (PHP extension)
add_library(bprof SHARED ${SOURCE_FILES})

# Link the PHP libraries
target_link_libraries(bprof ${PHP_EXTENSION_DIR}/libphp.so)  # Adjust this to match your PHP library path

# Set the output directory for the compiled extension
set_target_properties(bprof PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        PREFIX ""
)

# Add definitions
add_definitions(-DHAVE_CONFIG_H)

# Optionally, add any additional include directories
#include_directories(/path/to/other/includes)

# Optionally, set compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -fdeclspec")

# Add PHP-specific compile definitions
add_compile_definitions(
        ZEND_WIN32
        BPROF_DEBUG=false
)

# Install the extension to the PHP extensions directory
install(TARGETS bprof LIBRARY DESTINATION ${PHP_EXTENSION_DIR})