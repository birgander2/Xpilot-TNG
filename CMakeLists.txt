cmake_minimum_required(VERSION 3.25)
project(xpilot-tng VERSION 5.0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Options
option(BUILD_SERVER "Build the xpilot-tng-server" ON)
option(BUILD_CLIENT_SDL3 "Build modern SDL3 client (xpilot-tng-client)" ON)
option(BUILD_LAUNCHER "Build modern Dear ImGui / SDL3 frontend launcher (xpilot-tng-launcher)" ON)
option(FETCH_SDL3 "Download and build SDL3 / SDL3_ttf if not installed on system" OFF)

# Compiler Definitions & Flags
add_compile_definitions(_GNU_SOURCE HAVE_CONFIG_H)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
endif()

# Find System Dependencies
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(EXPAT REQUIRED)

# Check SDL3 and SDL3_ttf for modern client
find_package(SDL3 QUIET)
find_package(SDL3_ttf QUIET)

if(NOT TARGET SDL3::SDL3 OR NOT TARGET SDL3_ttf::SDL3_ttf)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        if(NOT TARGET SDL3::SDL3)
            pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
            if(TARGET PkgConfig::SDL3)
                add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
            endif()
        endif()
        if(NOT TARGET SDL3_ttf::SDL3_ttf)
            pkg_check_modules(SDL3_TTF QUIET IMPORTED_TARGET sdl3-ttf)
            if(TARGET PkgConfig::SDL3_TTF)
                add_library(SDL3_ttf::SDL3_ttf ALIAS PkgConfig::SDL3_TTF)
            endif()
        endif()
    endif()
endif()

# Check headers and functions for config.h
include(CheckIncludeFiles)
include(CheckFunctionExists)

check_include_files("unistd.h" HAVE_UNISTD_H)
check_include_files("sys/time.h" HAVE_SYS_TIME_H)
check_include_files("sys/types.h" HAVE_SYS_TYPES_H)
check_include_files("sys/stat.h" HAVE_SYS_STAT_H)
check_include_files("sys/socket.h" HAVE_SYS_SOCKET_H)
check_include_files("sys/select.h" HAVE_SYS_SELECT_H)
check_include_files("sys/ioctl.h" HAVE_SYS_IOCTL_H)
check_include_files("sys/param.h" HAVE_SYS_PARAM_H)
check_include_files("sys/file.h" HAVE_SYS_FILE_H)
check_include_files("sys/mman.h" HAVE_SYS_MMAN_H)
check_include_files("net/if.h" HAVE_NET_IF_H)
check_include_files("netinet/in.h" HAVE_NETINET_IN_H)
check_include_files("netinet/tcp.h" HAVE_NETINET_TCP_H)
check_include_files("arpa/inet.h" HAVE_ARPA_INET_H)
check_include_files("netdb.h" HAVE_NETDB_H)
check_include_files("string.h" HAVE_STRING_H)
check_include_files("strings.h" HAVE_STRINGS_H)
check_include_files("stdlib.h" HAVE_STDLIB_H)
check_include_files("stdint.h" HAVE_STDINT_H)
check_include_files("stddef.h" HAVE_STDDEF_H)
check_include_files("stdarg.h" HAVE_STDARG_H)
check_include_files("fcntl.h" HAVE_FCNTL_H)
check_include_files("signal.h" HAVE_SIGNAL_H)
check_include_files("math.h" HAVE_MATH_H)
check_include_files("errno.h" HAVE_ERRNO_H)
check_include_files("ctype.h" HAVE_CTYPE_H)
check_include_files("assert.h" HAVE_ASSERT_H)
check_include_files("limits.h" HAVE_LIMITS_H)
check_include_files("time.h" HAVE_TIME_H)
check_include_files("pwd.h" HAVE_PWD_H)
check_include_files("setjmp.h" HAVE_SETJMP_H)

check_function_exists(select HAVE_SELECT)
check_function_exists(socket HAVE_SOCKET)
check_function_exists(strcasecmp HAVE_STRCASECMP)
check_function_exists(strncasecmp HAVE_STRNCASECMP)
check_function_exists(strdup HAVE_STRDUP)
check_function_exists(strerror HAVE_STRERROR)
check_function_exists(strlcpy HAVE_STRLCPY)
check_function_exists(strlcat HAVE_STRLCAT)
check_function_exists(memmove HAVE_MEMMOVE)
check_function_exists(memset HAVE_MEMSET)
check_function_exists(gettimeofday HAVE_GETTIMEOFDAY)
set(CMAKE_REQUIRED_LIBRARIES m)
check_function_exists(pow HAVE_POW)
check_function_exists(sqrt HAVE_SQRT)
check_function_exists(rint HAVE_RINT)
check_function_exists(floor HAVE_FLOOR)
unset(CMAKE_REQUIRED_LIBRARIES)

set(SELECT_SCHED 1)

# Default data installation directory
include(GNUInstallDirs)
set(CONF_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/xpilot-tng/")
add_compile_definitions(CONF_DATADIR="${CONF_DATADIR}")

# Generate config.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# -------------------------------------------------------------
# Common Library: xpcommon
# -------------------------------------------------------------
set(COMMON_SOURCES
    src/common/arraylist.c
    src/common/checknames.c
    src/common/config.c
    src/common/error.c
    src/common/list.c
    src/common/math.c
    src/common/net.c
    src/common/portability.c
    src/common/randommt.c
    src/common/shipshape.c
    src/common/socklib.c
    src/common/strcasecmp.c
    src/common/strdup.c
    src/common/strlcpy.c
    src/common/xpmemory.c
)

add_library(xpcommon STATIC ${COMMON_SOURCES})
target_link_libraries(xpcommon PUBLIC m)

# -------------------------------------------------------------
# Server Target: xpilot-ng-server
# -------------------------------------------------------------
if(BUILD_SERVER)
    set(SERVER_SOURCES
        src/server/alliance.c
        src/server/asteroid.c
        src/server/cannon.c
        src/server/cell.c
        src/server/cmdline.c
        src/server/collision.c
        src/server/command.c
        src/server/contact.c
        src/server/event.c
        src/server/fileparser.c
        src/server/frame.c
        src/server/gravity.c
        src/server/id.c
        src/server/item.c
        src/server/laser.c
        src/server/map.c
        src/server/metaserver.c
        src/server/modifiers.c
        src/server/netserver.c
        src/server/object.c
        src/server/objpos.c
        src/server/option.c
        src/server/parser.c
        src/server/player.c
        src/server/polygon.c
        src/server/race.c
        src/server/rank.c
        src/server/recwrap.c
        src/server/robot.c
        src/server/robotdef.c
        src/server/rules.c
        src/server/saudio.c
        src/server/sched.c
        src/server/score.c
        src/server/server.c
        src/server/ship.c
        src/server/shot.c
        src/server/showtime.c
        src/server/smartbot.c
        src/server/srecord.c
        src/server/suibotdef.c
        src/server/tag.c
        src/server/target.c
        src/server/teamcup.c
        src/server/tuner.c
        src/server/treasure.c
        src/server/update.c
        src/server/walls.c
        src/server/wormhole.c
        src/server/xp2map.c
        src/server/xpmap.c
    )

    add_executable(xpilot-tng-server ${SERVER_SOURCES})
    target_include_directories(xpilot-tng-server PRIVATE src/server)
    target_link_libraries(xpilot-tng-server PRIVATE xpcommon EXPAT::EXPAT ZLIB::ZLIB m Threads::Threads)

    install(TARGETS xpilot-tng-server DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# -------------------------------------------------------------
# Client Core Library: xpclient
# -------------------------------------------------------------
set(CLIENT_SOURCES
    src/client/caudio.c
    src/client/client.c
    src/client/clientcommand.c
    src/client/clientrank.c
    src/client/datagram.c
    src/client/default.c
    src/client/event.c
    src/client/gfx2d.c
    src/client/mapdata.c
    src/client/messages.c
    src/client/meta.c
    src/client/netclient.c
    src/client/paint.c
    src/client/paintmap.c
    src/client/paintobjects.c
    src/client/option.c
    src/client/query.c
    src/client/talkmacros.c
    src/client/textinterface.c
    src/client/usleep.c
)

add_library(xpclient STATIC ${CLIENT_SOURCES})
target_include_directories(xpclient PUBLIC src/client)
target_link_libraries(xpclient PUBLIC xpcommon ZLIB::ZLIB EXPAT::EXPAT)

# -------------------------------------------------------------
# Modern SDL3 Client Target
# -------------------------------------------------------------
if(BUILD_CLIENT_SDL3)
    if(NOT (TARGET SDL3::SDL3 AND TARGET SDL3_ttf::SDL3_ttf) AND FETCH_SDL3)
        include(FetchContent)
        message(STATUS "FETCH_SDL3 enabled: downloading and compiling SDL3 & SDL3_ttf...")
        if(NOT TARGET SDL3::SDL3)
            FetchContent_Declare(
                SDL3
                GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                GIT_TAG release-3.2.8
            )
            FetchContent_MakeAvailable(SDL3)
        endif()
        if(NOT TARGET SDL3_ttf::SDL3_ttf)
            FetchContent_Declare(
                SDL3_ttf
                GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
                GIT_TAG release-3.1.0
            )
            FetchContent_MakeAvailable(SDL3_ttf)
        endif()
    endif()

    if(TARGET SDL3::SDL3 AND TARGET SDL3_ttf::SDL3_ttf)
        message(STATUS "SDL3 and SDL3_ttf found! Enabling modern SDL3 client.")
        add_subdirectory(src/client/sdl3)
    else()
        message(WARNING "SDL3 or SDL3_ttf not found on system. Modern client (xpilot-tng-client) skipped.\n"
                        "  To build the client, please install the development packages:\n"
                        "    Ubuntu / Debian: sudo apt install libsdl3-dev libsdl3-ttf-dev\n"
                        "    openSUSE:        sudo zypper in SDL3-devel SDL3_ttf-devel\n"
                        "    Fedora:          sudo dnf install SDL3-devel SDL3_ttf-devel\n"
                        "    Arch Linux:      sudo pacman -S sdl3 sdl3_ttf\n"
                        "  Alternatively, re-run CMake with automatic download:\n"
                        "    cmake -B build -DFETCH_SDL3=ON")
    endif()
endif()

# -------------------------------------------------------------
# Modern Dear ImGui / SDL3 Launcher Target
# -------------------------------------------------------------
if(BUILD_LAUNCHER)
    if(TARGET SDL3::SDL3)
        message(STATUS "SDL3 found! Enabling modern launcher (xpilot-tng-launcher).")
        add_subdirectory(src/launcher)
    else()
        message(WARNING "SDL3 not found on system. Launcher (xpilot-tng-launcher) skipped.")
    endif()
endif()

# -------------------------------------------------------------
# Install Game Assets
# -------------------------------------------------------------
install(DIRECTORY lib/ DESTINATION ${CONF_DATADIR}
    PATTERN "Makefile*" EXCLUDE
)
