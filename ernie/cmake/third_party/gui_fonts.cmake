set(ERNIE_DEP_VERSION_inter
    v4.1
    CACHE STRING "Tag of the Inter typeface to fetch.")
set(ERNIE_DEP_HASH_inter
    4989b125924991b90d05b2d16e0e388c48f7d5bb8b30539bbf9c755278d0ccaf
    CACHE STRING "SHA-256 of InterVariable.ttf at ERNIE_DEP_VERSION_inter.")
set(ERNIE_DEP_VERSION_lucide
    1.52.0
    CACHE STRING "Version of the Lucide icon font (lucide-static) to fetch.")
set(ERNIE_DEP_HASH_lucide
    124ecc64b91a158fc519eefdaaead89a2d9e2c39b6f9c8429c5584c93a0be451
    CACHE STRING "SHA-256 of lucide.ttf at ERNIE_DEP_VERSION_lucide.")
mark_as_advanced(ERNIE_DEP_VERSION_inter ERNIE_DEP_HASH_inter ERNIE_DEP_VERSION_lucide
                 ERNIE_DEP_HASH_lucide)

include(FetchContent)

# Inter (SIL OFL 1.1) for text and Lucide (ISC) for icons, each a single TTF.
FetchContent_Declare(
    ernie_font_inter
    URL "https://raw.githubusercontent.com/rsms/inter/${ERNIE_DEP_VERSION_inter}/docs/font-files/InterVariable.ttf"
    URL_HASH SHA256=${ERNIE_DEP_HASH_inter}
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_Declare(
    ernie_font_lucide
    URL "https://cdn.jsdelivr.net/npm/lucide-static@${ERNIE_DEP_VERSION_lucide}/font/lucide.ttf"
    URL_HASH SHA256=${ERNIE_DEP_HASH_lucide}
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(ernie_font_inter ernie_font_lucide)

# The fonts are compiled into the GUI as byte arrays, so it needs no data files at run time.
set(ernie_gui_font_dir "${CMAKE_BINARY_DIR}/_deps/ernie_gui_fonts")
set(ernie_gui_font_sources)
foreach(font inter lucide)
    file(GLOB font_file "${ernie_font_${font}_SOURCE_DIR}/*.ttf")
    set(font_source "${ernie_gui_font_dir}/${font}_font.cpp")
    add_custom_command(
        OUTPUT "${font_source}"
        COMMAND
            "${CMAKE_COMMAND}" -DINPUT=${font_file} -DOUTPUT=${font_source}
            -DSYMBOL=kErnieFont_${font} -P "${CMAKE_CURRENT_LIST_DIR}/embed_file.cmake"
        DEPENDS "${font_file}" "${CMAKE_CURRENT_LIST_DIR}/embed_file.cmake"
        COMMENT "Embedding the ${font} font"
        VERBATIM)
    list(APPEND ernie_gui_font_sources "${font_source}")
endforeach()
add_library(ernie_gui_fonts STATIC ${ernie_gui_font_sources})
