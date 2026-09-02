cmake_minimum_required(VERSION 3.20)
project(xcord
  VERSION 0.0.1
  LANGUAGES C
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()
option(ENABLE_SANITIZERS "Enable sanitizers (ASan, UBSan)" OFF)
add_library(xcord_options INTERFACE)
if(MSVC)
  target_compile_options(xcord_options INTERFACE /W4 /WX)
  target_compile_options(xcord_options INTERFACE $<$<CONFIG:Release>:/O2>)
else()
  target_compile_options(xcord_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
  target_compile_options(xcord_options INTERFACE $<$<CONFIG:Release>:-O3>)
  target_compile_options(xcord_options INTERFACE $<$<CONFIG:Release>:-march=native>)
  target_compile_options(xcord_options INTERFACE $<$<CONFIG:Release>:-flto>)
  target_link_options(xcord_options INTERFACE $<$<CONFIG:Release>:-flto>)
  if(ENABLE_SANITIZERS)
    target_compile_options(xcord_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(xcord_options INTERFACE -fsanitize=address,undefined)
  endif()
endif()
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(sodium REQUIRED IMPORTED_TARGET GLOBAL libsodium)
include(FetchContent)
FetchContent_Declare(
    yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.12.0
)
set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YYJSON_BUILD_DOCS  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yyjson)
FetchContent_Declare(
    cargs
    GIT_REPOSITORY https://github.com/likle/cargs.git
    GIT_TAG v1.2.0
)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_DEMO OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cargs)
add_library(common
  src/common/strbuf.c
  src/common/base64.c
  src/common/tty.c
  src/common/strutil.c
  src/common/timeutil.c
)
target_include_directories(common PUBLIC src/)
target_link_libraries(common PRIVATE xcord_options)
add_library(net
  src/net/ssl.c
  src/net/http.c
  src/net/err.c
  src/net/ratelimit.c
)
target_include_directories(net PUBLIC src/)
target_link_libraries(net PUBLIC OpenSSL::SSL OpenSSL::Crypto PRIVATE common xcord_options)
if(WIN32)
  target_link_libraries(net PRIVATE ws2_32)
endif()
add_library(credentials
    src/credentials/credentials.c
)
target_include_directories(credentials PUBLIC src/)
target_link_libraries(credentials PUBLIC PkgConfig::sodium PRIVATE xcord_options)
add_library(discord
  src/discord/discord.c
)
target_include_directories(discord PUBLIC src/)
target_link_libraries(discord PRIVATE OpenSSL::SSL net common yyjson xcord_options)
add_executable(main
  src/main.c
  src/cli/cli.c
  src/cli/scope.c
  src/cli/filters.c
  src/cli/cmd_login.c
  src/cli/cmd_whoami.c
  src/cli/cmd_guilds.c
  src/cli/cmd_logout.c
  src/cli/cmd_purge.c
  src/cli/cmd_channels.c
)
target_link_libraries(main PRIVATE discord net common credentials cargs xcord_options)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB_RECURSE ALL_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/include/*.h
  )
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
    VERBATIM
  )
  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCE_FILES}
    VERBATIM
  )
endif()
