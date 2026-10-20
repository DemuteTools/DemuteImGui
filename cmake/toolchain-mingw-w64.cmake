# Cross compilation to Windows with MinGW-w64, from Linux / WSL: a check that the Windows-only `host`
# target compiles and links (the tools themselves are built with MSVC on Windows). The executables
# are not run here.
#
#   cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake [-DMINGW_ROOT=<root>]
#
# MINGW_ROOT: the prefix holding usr/bin/x86_64-w64-mingw32-g++-posix (a MinGW unpacked without rights);
# empty = the compilers on the PATH.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MINGW_ROOT)

set(MINGW_ROOT "${MINGW_ROOT}" CACHE PATH "Prefix of a MinGW-w64 installation (holds usr/bin)")
set(_mingw_prefix x86_64-w64-mingw32)
if(MINGW_ROOT)
    set(_mingw_bin "${MINGW_ROOT}/usr/bin/")
else()
    set(_mingw_bin "")
endif()

find_program(_mingw_cxx NAMES ${_mingw_prefix}-g++-posix ${_mingw_prefix}-g++
             HINTS "${_mingw_bin}" NO_CACHE)
find_program(_mingw_cc NAMES ${_mingw_prefix}-gcc-posix ${_mingw_prefix}-gcc
             HINTS "${_mingw_bin}" NO_CACHE)
find_program(_mingw_rc NAMES ${_mingw_prefix}-windres HINTS "${_mingw_bin}" NO_CACHE)
if(NOT _mingw_cxx)
    message(FATAL_ERROR "MinGW-w64 g++ not found (set MINGW_ROOT to the prefix holding usr/bin)")
endif()
set(CMAKE_CXX_COMPILER "${_mingw_cxx}")
if(_mingw_cc)
    set(CMAKE_C_COMPILER "${_mingw_cc}")
endif()
if(_mingw_rc)
    set(CMAKE_RC_COMPILER "${_mingw_rc}")
endif()

# Headers and libraries of the target only; programs (git, cmake) of the build machine.
if(MINGW_ROOT)
    set(CMAKE_FIND_ROOT_PATH "${MINGW_ROOT}/usr/${_mingw_prefix}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Static libgcc / libstdc++: the check executable needs no MinGW DLL beside it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
