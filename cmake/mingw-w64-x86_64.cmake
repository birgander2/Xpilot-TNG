# Cross-compile toolchain for 64-bit Windows using MinGW-w64 (Debian/Ubuntu packages).
# Usage: cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
# The "-posix" compiler variant is required: the launcher uses std::thread.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TRIPLET x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${MINGW_TRIPLET}-gcc-posix)
set(CMAKE_CXX_COMPILER ${MINGW_TRIPLET}-g++-posix)
set(CMAKE_RC_COMPILER  ${MINGW_TRIPLET}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TRIPLET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Never pick up the host's pkg-config files (an empty value means "default" to pkg-config)
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_FIND_ROOT_PATH}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_FIND_ROOT_PATH}")

# Run test programs (try_run) through wine if it is available
find_program(WINE_EXECUTABLE wine)
if(WINE_EXECUTABLE)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${WINE_EXECUTABLE})
endif()
