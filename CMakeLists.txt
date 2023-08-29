cmake_minimum_required(VERSION 3.5)

project(extname
        VERSION 1.0.0
        LANGUAGES C)

execute_process (
        COMMAND php-config --include-dir
        OUTPUT_VARIABLE PHP_SOURCE
)

string(REGEX REPLACE "\n$" "" PHP_SOURCE "${PHP_SOURCE}")

message("Using source directory: ${PHP_SOURCE}")

include_directories(
        "${PHP_SOURCE}"
        "${PHP_SOURCE}/main"
        "${PHP_SOURCE}/Zend"
        "${PHP_SOURCE}/TSRM"
        "${PHP_SOURCE}/ext"
        "${PHP_SOURCE}/sapi"
        "${PROJECT_SOURCE_DIR}")
