cmake_minimum_required(VERSION 3.25)
project(xpilot-tng VERSION 5.0.2 LANGUAGES C CXX)

# Without a build type CMake compiles without optimisation, which makes the
# server's robots several times slower (too slow for a Raspberry Pi).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Release, RelWithDebInfo, Debug)" FORCE)
endif()

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
option(BUILD_MAPEDITOR "Build the Dear ImGui / SDL3 map editor (xpilot-tng-mapeditor)" ON)
option(SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (for testing)" OFF)
option(BUILD_TESTS "Build the security regression and fuzz tests" OFF)
option(FETCH_SDL3 "Download and build SDL3 / SDL3_ttf if not installed on system" OFF)
option(FETCH_OPUS "Download and build Opus (static) if not installed on system" OFF)
# Windows builds have no system libraries to link against: fetch everything by default
if(WIN32)
    set(_fetch_default ON)
else()
    set(_fetch_default OFF)
endif()
option(FETCH_DEPS "Download and build zlib / expat (static) instead of using system packages" ${_fetch_default})
if(WIN32 AND FETCH_DEPS)
    set(FETCH_SDL3 ON CACHE BOOL "" FORCE)
    set(FETCH_OPUS ON CACHE BOOL "" FORCE)
endif()

# Compiler Definitions & Flags
add_compile_definitions(_GNU_SOURCE HAVE_CONFIG_H)
if(WIN32)
    # Windows Vista+ API (inet_ntop, WSAPoll, GetAdaptersAddresses); no min/max macros
    # FD_SETSIZE: Winsock's default fd_set holds only 64 sockets
    add_compile_definitions(_WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX
                            __USE_MINGW_ANSI_STDIO=1 FD_SETSIZE=1024)
    # Self-contained executables: no libgcc / libstdc++ / winpthread DLLs
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)

    # Hardening (FORTIFY_SOURCE needs optimization, so skip it for Debug)
    option(HARDENING "Enable compiler/linker hardening flags" ON)
    if(HARDENING AND WIN32)
        add_compile_options(-fstack-protector-strong)
        add_link_options(-fstack-protector-strong
                         -Wl,--dynamicbase,--nxcompat,--high-entropy-va)
    elseif(HARDENING)
        add_compile_options(-fstack-protector-strong -fstack-clash-protection
                            -fno-strict-overflow -fPIE
                            $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
                            $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
        add_link_options(-pie -Wl,-z,relro,-z,now)
    endif()
endif()

if(SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address,undefined)
endif()

# Find System Dependencies
find_package(Threads REQUIRED)
if(FETCH_DEPS)
    include(FetchContent)
    message(STATUS "FETCH_DEPS enabled: downloading and compiling zlib & expat...")
    set(_fetch_exclude)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        set(_fetch_exclude EXCLUDE_FROM_ALL)
    endif()

    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
        ${_fetch_exclude})
    FetchContent_MakeAvailable(zlib)
    # zlib's own targets carry no usage requirements; wrap the static one
    add_library(xp_zlib INTERFACE)
    target_link_libraries(xp_zlib INTERFACE zlibstatic)
    target_include_directories(xp_zlib INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
    add_library(ZLIB::ZLIB ALIAS xp_zlib)

    set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(EXPAT_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(EXPAT_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(expat
        URL https://github.com/libexpat/libexpat/releases/download/R_2_6_4/expat-2.6.4.tar.xz
        ${_fetch_exclude})
    FetchContent_MakeAvailable(expat)
    add_library(xp_expat INTERFACE)
    target_link_libraries(xp_expat INTERFACE expat)
    target_include_directories(xp_expat INTERFACE ${expat_SOURCE_DIR}/lib ${expat_BINARY_DIR})
    target_compile_definitions(xp_expat INTERFACE XML_STATIC)
    add_library(EXPAT::EXPAT ALIAS xp_expat)
else()
    find_package(ZLIB REQUIRED)
    find_package(EXPAT REQUIRED)
endif()

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
if(WIN32)
    # Windows builds are always relocatable bundles: lib/ next to the executables
    set(CONF_DATADIR "lib/")
else()
    set(CONF_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/xpilot-tng/")
endif()
add_compile_definitions(CONF_DATADIR="${CONF_DATADIR}")

# Generate config.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

# Windows: icon and version resource, linked into every executable (see below)
if(WIN32)
    enable_language(RC)
    set(XP_WIN_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/common/win/xpilot-tng.ico")
    configure_file(src/common/win/xpilot-tng.rc.in ${CMAKE_CURRENT_BINARY_DIR}/xpilot-tng.rc @ONLY)
endif()

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
if(WIN32)
    target_link_libraries(xpcommon PUBLIC ws2_32 iphlpapi bcrypt winmm)
endif()

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
        src/server/ballbot.c
        src/server/racebot.c
        src/server/srecord.c
        src/server/tag.c
        src/server/target.c
        src/server/teamcup.c
        src/server/tuner.c
        src/server/treasure.c
        src/server/update.c
        src/server/voicechat.c
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
    src/client/newbie.c
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
        if(WIN32)
            # Static SDL3 / SDL3_ttf with bundled FreeType: no DLLs to ship
            set(SDL_SHARED OFF CACHE BOOL "" FORCE)
            set(SDL_STATIC ON CACHE BOOL "" FORCE)
            set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
            set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
            set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
            set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
            set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
            set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
            set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        endif()
        if(NOT TARGET SDL3::SDL3)
            FetchContent_Declare(
                SDL3
                GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                GIT_TAG release-3.4.2
                GIT_SHALLOW TRUE
            )
            FetchContent_MakeAvailable(SDL3)
        endif()
        if(NOT TARGET SDL3_ttf::SDL3_ttf)
            FetchContent_Declare(
                SDL3_ttf
                GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
                GIT_TAG release-3.2.2
                GIT_SHALLOW TRUE
                GIT_SUBMODULES external/freetype
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
# Map Editor Target
# -------------------------------------------------------------
if(BUILD_MAPEDITOR)
    if(TARGET SDL3::SDL3)
        add_subdirectory(src/mapeditor)
    else()
        message(WARNING "SDL3 not found on system. Map editor (xpilot-tng-mapeditor) skipped.")
    endif()
endif()

# -------------------------------------------------------------
# Install Game Assets
# -------------------------------------------------------------
install(DIRECTORY lib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/xpilot-tng
    PATTERN "Makefile*" EXCLUDE
)

# -------------------------------------------------------------
# Relocatable bundle: "cmake --build build --target dist"
# Binaries and lib/ side by side; the result can be moved anywhere.
# Override the location with -DDIST_DIR=/some/path
# -------------------------------------------------------------
set(DIST_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Output directory of the 'dist' target")
add_custom_target(dist
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/lib" "${DIST_DIR}/lib"
    COMMENT "Creating relocatable bundle in ${DIST_DIR}"
)
foreach(_t xpilot-tng-server xpilot-tng-client xpilot-tng-launcher xpilot-tng-mapeditor)
    if(TARGET ${_t})
        if(WIN32)
            target_sources(${_t} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/xpilot-tng.rc)
        endif()
        add_dependencies(dist ${_t})
        add_custom_command(TARGET dist POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${_t}>" "${DIST_DIR}/")
        if(NOT WIN32)
            add_custom_command(TARGET dist POST_BUILD
                COMMAND chmod 755 "${DIST_DIR}/$<TARGET_FILE_NAME:${_t}>")
        endif()
    endif()
endforeach()

# -------------------------------------------------------------
# Packaging (.deb): use tools/make-deb.sh
# -------------------------------------------------------------
set(CPACK_PACKAGE_NAME "xpilot-tng")
set(CPACK_PACKAGE_VENDOR "XPilot TNG")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "XPilot TNG multiplayer space war game")
set(CPACK_PACKAGE_CONTACT "XPilot TNG maintainers <github@nought.de>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)

# -------------------------------------------------------------
# Tests (cmake -DBUILD_TESTS=ON -DSANITIZE=ON; run with ctest)
# -------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    add_executable(fuzz_parsers tests/fuzz_parsers.c)
    target_link_libraries(fuzz_parsers PRIVATE xpcommon)
    add_test(NAME fuzz_parsers COMMAND fuzz_parsers 200000)
    if(BUILD_SERVER)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME server_security
                     COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/server_security.py
                             $<TARGET_FILE:xpilot-tng-server>)
        endif()
    endif()
endif()
