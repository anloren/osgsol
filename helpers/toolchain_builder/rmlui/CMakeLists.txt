# RmlUi product-UI dependency. The release is downloaded only when
# OSGSOL_BUILD_RMLUI_PRODUCT_UI=ON; normal/legacy builds remain network-free.
SET(OSGSOL_RMLUI_VERSION "6.2")
SET(OSGSOL_RMLUI_URL
    "https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz")
SET(OSGSOL_RMLUI_SHA256
    "814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b")
SET(OSGSOL_RMLUI_CACHE_DIR "${CMAKE_BINARY_DIR}/_deps")
SET(OSGSOL_RMLUI_ARCHIVE
    "${OSGSOL_RMLUI_CACHE_DIR}/RmlUi-${OSGSOL_RMLUI_VERSION}.tar.gz")
SET(OSGSOL_RMLUI_SOURCE_DIR
    "${OSGSOL_RMLUI_CACHE_DIR}/RmlUi-${OSGSOL_RMLUI_VERSION}")

FILE(MAKE_DIRECTORY "${OSGSOL_RMLUI_CACHE_DIR}")
IF(NOT EXISTS "${OSGSOL_RMLUI_ARCHIVE}")
    MESSAGE(STATUS "[osgSol] Downloading pinned RmlUi ${OSGSOL_RMLUI_VERSION}")
    FILE(DOWNLOAD "${OSGSOL_RMLUI_URL}" "${OSGSOL_RMLUI_ARCHIVE}"
         EXPECTED_HASH "SHA256=${OSGSOL_RMLUI_SHA256}"
         TLS_VERIFY ON
         SHOW_PROGRESS
         STATUS OSGSOL_RMLUI_DOWNLOAD_STATUS)
    LIST(GET OSGSOL_RMLUI_DOWNLOAD_STATUS 0 OSGSOL_RMLUI_DOWNLOAD_CODE)
    IF(NOT OSGSOL_RMLUI_DOWNLOAD_CODE EQUAL 0)
        LIST(GET OSGSOL_RMLUI_DOWNLOAD_STATUS 1 OSGSOL_RMLUI_DOWNLOAD_MESSAGE)
        MESSAGE(FATAL_ERROR
            "Pinned RmlUi download failed: ${OSGSOL_RMLUI_DOWNLOAD_MESSAGE}")
    ENDIF()
ELSE()
    FILE(SHA256 "${OSGSOL_RMLUI_ARCHIVE}" OSGSOL_RMLUI_ARCHIVE_SHA256)
    IF(NOT OSGSOL_RMLUI_ARCHIVE_SHA256 STREQUAL OSGSOL_RMLUI_SHA256)
        MESSAGE(FATAL_ERROR
            "Cached RmlUi archive hash mismatch; remove only ${OSGSOL_RMLUI_ARCHIVE} and reconfigure")
    ENDIF()
ENDIF()

IF(NOT EXISTS "${OSGSOL_RMLUI_SOURCE_DIR}/CMakeLists.txt")
    EXECUTE_PROCESS(
        COMMAND "${CMAKE_COMMAND}" -E tar xzf "${OSGSOL_RMLUI_ARCHIVE}"
        WORKING_DIRECTORY "${OSGSOL_RMLUI_CACHE_DIR}"
        RESULT_VARIABLE OSGSOL_RMLUI_EXTRACT_RESULT)
    IF(NOT OSGSOL_RMLUI_EXTRACT_RESULT EQUAL 0 OR
       NOT EXISTS "${OSGSOL_RMLUI_SOURCE_DIR}/CMakeLists.txt")
        MESSAGE(FATAL_ERROR "Unable to extract pinned RmlUi source archive")
    ENDIF()
ENDIF()

# Retain the upstream MIT license next to build products for packaging/audit.
FILE(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/licenses")
CONFIGURE_FILE("${OSGSOL_RMLUI_SOURCE_DIR}/LICENSE.txt"
               "${CMAKE_BINARY_DIR}/licenses/RmlUi-6.2-LICENSE.txt" COPYONLY)

# Reuse osgVerse's already-built static FreeType. RmlUi sees its canonical
# target name while the rest of the project remains unchanged.
IF(TARGET freetype AND NOT TARGET Freetype::Freetype)
    ADD_LIBRARY(osgsol_rmlui_freetype INTERFACE)
    TARGET_LINK_LIBRARIES(osgsol_rmlui_freetype INTERFACE freetype)
    TARGET_INCLUDE_DIRECTORIES(osgsol_rmlui_freetype INTERFACE
        "${CMAKE_SOURCE_DIR}/helpers/toolchain_builder/freetype/include")
    ADD_LIBRARY(Freetype::Freetype ALIAS osgsol_rmlui_freetype)
ENDIF()
IF(NOT TARGET Freetype::Freetype)
    MESSAGE(FATAL_ERROR "RmlUi product UI requires the existing FreeType target")
ENDIF()

# These normal variables are directory-scoped, so RmlUi becomes static without
# changing osgVerse's own BUILD_SHARED_LIBS policy.
SET(BUILD_SHARED_LIBS OFF)
SET(RMLUI_SAMPLES OFF)
SET(RMLUI_LUA_BINDINGS OFF)
SET(RMLUI_LOTTIE_PLUGIN OFF)
SET(RMLUI_SVG_PLUGIN OFF)
SET(RMLUI_FONT_ENGINE "freetype")
SET(RMLUI_PRECOMPILED_HEADERS OFF)
SET(RMLUI_INSTALL_LICENSES_AND_BUILD_INFO OFF)

ADD_SUBDIRECTORY("${OSGSOL_RMLUI_SOURCE_DIR}"
                 "${CMAKE_BINARY_DIR}/_deps/rmlui-build" EXCLUDE_FROM_ALL)

SET(OSGSOL_RMLUI_SOURCE_DIR "${OSGSOL_RMLUI_SOURCE_DIR}" PARENT_SCOPE)
