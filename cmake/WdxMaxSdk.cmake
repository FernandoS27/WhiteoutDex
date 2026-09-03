# ============================================================================
# WhiteoutDex — 3ds Max SDK discovery
#
# One place both build entry points agree on where a Max SDK lives: the root
# CMakeLists' multi-version detection loop, and src/native_sources' single-
# version MAXSDK_PATH. They used to carry two copies of the same hardcoded
# "C:/Program Files/Autodesk/3ds Max <year> SDK" string.
#
# The SDK is a separate download from the product and neither has to sit on
# C:. A machine with Max 2027 under D:\Programs\Autodesk\3ds Max 2027 and the
# older SDKs under C:\Program Files\Autodesk would have had its 2027 SDK
# silently skipped by a single hardcoded root — and "no 2027 in the detected
# list" reads exactly like "2027 isn't supported", which is the wrong
# diagnosis to hand someone. So: explicit override first, then the roots we
# know about, then whatever root the installed product reports to the
# registry.
# ============================================================================

set(WDX_MAX_SDK_ROOTS "" CACHE STRING
    "Extra directories to search for '3ds Max <year> SDK' (semicolon-separated)")

# ----------------------------------------------------------------------------
# Product install dir for @p year, from the key Max writes at install time:
# HKLM\SOFTWARE\Autodesk\3dsMax\<year - 1998>.0\Installdir (2016 = 18.0).
# That key is the only pointer to a product installed off the default drive.
# Sets @p out_var to "" when the product isn't installed or the registry
# isn't readable (non-Windows, or a CMake too old for the query).
# ----------------------------------------------------------------------------
function(wdx_max_install_dir year out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT CMAKE_HOST_WIN32 OR CMAKE_VERSION VERSION_LESS 3.24)
        return()
    endif()

    math(EXPR _major "${year} - 1998")
    cmake_host_system_information(RESULT _dir
        QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Autodesk/3dsMax/${_major}.0"
        VALUE "Installdir" VIEW 64
        ERROR_VARIABLE _err)

    if(_err OR NOT _dir)
        return()
    endif()
    # Max stores it with a trailing separator ("D:\Programs\...\3ds Max 2027\").
    file(TO_CMAKE_PATH "${_dir}" _dir)
    string(REGEX REPLACE "/+$" "" _dir "${_dir}")
    set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()

# ----------------------------------------------------------------------------
# Resolve the maxsdk directory for @p year into @p out_var, or "" when the SDK
# isn't installed. A directory only counts once include/max.h is in it: an
# empty "3ds Max 2027 SDK" folder is what a cancelled install leaves behind,
# and letting that through turns a clear configure-time message into a wall of
# missing-header errors halfway through the build.
# ----------------------------------------------------------------------------
function(wdx_find_max_sdk year out_var)
    set(${out_var} "" PARENT_SCOPE)

    # 1. Per-year override, for an SDK unpacked somewhere unguessable.
    if(DEFINED MAXSDK_PATH_${year} AND EXISTS "${MAXSDK_PATH_${year}}/include/max.h")
        set(${out_var} "${MAXSDK_PATH_${year}}" PARENT_SCOPE)
        return()
    endif()

    set(_roots ${WDX_MAX_SDK_ROOTS})
    if(DEFINED ENV{ProgramFiles})
        file(TO_CMAKE_PATH "$ENV{ProgramFiles}" _pf)
        list(APPEND _roots "${_pf}/Autodesk")
    endif()
    list(APPEND _roots "C:/Program Files/Autodesk")

    # 2. Beside the installed product, which is where the SDK installer
    #    defaults to — same parent, "<product> SDK".
    wdx_max_install_dir(${year} _install_dir)
    if(_install_dir)
        get_filename_component(_install_parent "${_install_dir}" DIRECTORY)
        list(APPEND _roots "${_install_parent}")
    endif()

    list(REMOVE_DUPLICATES _roots)
    foreach(_root IN LISTS _roots)
        set(_candidate "${_root}/3ds Max ${year} SDK/maxsdk")
        if(EXISTS "${_candidate}/include/max.h")
            set(${out_var} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()
