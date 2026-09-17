# Concatenate a WhiteoutFlakes language catalog with the WhiteoutDex overlay.
#
# Invoked as:
#   cmake -DWDX_LANG_BASE=<flakes>.ini -DWDX_LANG_OVERLAY=<wdx_*>.ini \
#         -DWDX_LANG_OUT=<out>.ini -P MergeLangCatalog.cmake
#
# `cmake -E cat` writes to stdout and add_custom_command has no shell to
# redirect it, so the join happens here instead. The files are UTF-8 without a
# BOM and the loader reads them line by line doing insert_or_assign, so a plain
# append is a valid merge: the overlay's `wdx.*` keys are additive, and were it
# ever to repeat an upstream key, coming second is what would make it win.
#
# READ is used rather than `file(APPEND)` on the source so the output is
# rewritten from scratch on every run — a stale half-merged file from an
# interrupted build would otherwise survive.

if(NOT DEFINED WDX_LANG_BASE OR NOT DEFINED WDX_LANG_OUT)
    message(FATAL_ERROR "MergeLangCatalog.cmake: WDX_LANG_BASE and WDX_LANG_OUT are required")
endif()

file(READ "${WDX_LANG_BASE}" _base)
set(_overlay "")
if(DEFINED WDX_LANG_OVERLAY AND EXISTS "${WDX_LANG_OVERLAY}")
    file(READ "${WDX_LANG_OVERLAY}" _overlay)
endif()

# A base catalog that does not end in a newline would otherwise glue its last
# key onto the overlay's first comment line.
string(REGEX REPLACE "\n?$" "\n" _base "${_base}")

file(WRITE "${WDX_LANG_OUT}" "${_base}\n${_overlay}")
