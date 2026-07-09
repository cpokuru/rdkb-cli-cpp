cmake_minimum_required(VERSION 3.10)
project(em_cli_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MICROHTTPD REQUIRED libmicrohttpd)
pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
pkg_check_modules(CJSON REQUIRED libcjson)

# Path to em_cli_apis.h and libemcli.so, relative to this project.
# Default assumes this project lives at unified-wifi-mesh/src/rdkb-cli/em_cli_cpp/
# (i.e. nested one level inside rdkb-cli/, alongside em_cli.cpp).
# If you place it elsewhere, override on the command line, e.g.:
#   cmake .. -DEM_CLI_INC_DIR=../../../../inc -DEM_CLI_LIB_DIR=../../../../install/lib
set(EM_CLI_INC_DIR "../../../inc" CACHE STRING "Path to em_cli_apis.h")
set(EM_CLI_LIB_DIR "../../../install/lib" CACHE STRING "Path to libemcli.so")

# em_cli_apis.h pulls in em_base.h, which pulls in wifi_webconfig.h and
# friends — the same transitive chain the original Go build needed via
# CGO's "#cgo CFLAGS: -I../../../OneWifi/include
# -I../../../OneWifi/source/utils -I../../../halinterface/include" (all
# relative to rdkb-cli/, so one extra "../" here since this project is
# nested one level deeper). /usr/include/ccsp is also listed since
# wifi_webconfig.h was confirmed there. Paths that don't exist on your
# system are silently ignored by the compiler, so it's safe to list several
# candidates — add more via -DEM_CLI_EXTRA_INC_DIRS="path1;path2" if this
# chain pulls in still more headers.
set(EM_CLI_EXTRA_INC_DIRS
    "/usr/include/ccsp"
    "../../../../OneWifi/include"
    "../../../../OneWifi/source/utils"
    "../../../../halinterface/include"
    CACHE STRING "Additional include dirs for em_cli_apis.h's transitive headers"
)

add_executable(onewifi_em_cli
    src/main.cpp
    src/http_server.cpp
    src/ws_server.cpp
    src/json_helpers.cpp
    src/json_helpers_ext.cpp
    src/sample_data.cpp
    src/em_cli_bridge.cpp
    src/coverage.cpp
    src/handlers_wireless.cpp
    src/handlers_coverage.cpp
    src/handlers_topology.cpp
    src/handlers_performance.cpp
    src/handlers_misc.cpp
)

target_include_directories(onewifi_em_cli PRIVATE
    inc
    ${EM_CLI_INC_DIR}
    ${EM_CLI_EXTRA_INC_DIRS}
    ${MICROHTTPD_INCLUDE_DIRS}
    ${LIBWEBSOCKETS_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
)

link_directories(${EM_CLI_LIB_DIR})

target_link_libraries(onewifi_em_cli PRIVATE
    ${MICROHTTPD_LIBRARIES}
    ${LIBWEBSOCKETS_LIBRARIES}
    ${CJSON_LIBRARIES}
    emcli   # matches the original Makefile's -lemcli
    pthread
)

target_compile_options(onewifi_em_cli PRIVATE -Wall -Wextra -O2)
