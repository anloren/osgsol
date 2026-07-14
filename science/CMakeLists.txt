ADD_LIBRARY(osgSolSciencePreview STATIC
    SciencePreviewRuntime.cpp
    SciencePreviewRuntime.h)

TARGET_COMPILE_FEATURES(osgSolSciencePreview PUBLIC cxx_std_17)
TARGET_INCLUDE_DIRECTORIES(osgSolSciencePreview
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
SET_TARGET_PROPERTIES(osgSolSciencePreview PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden)

IF(NOT APPLE)
    MESSAGE(FATAL_ERROR "ScienceEarth preview currently targets macOS")
ENDIF()
IF(NOT EXISTS "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libgdal.a")
    MESSAGE(FATAL_ERROR
        "OSGSOL_SCIENCE_DEPS_ROOT must name the built private ScienceEarth prefix")
ENDIF()

EXECUTE_PROCESS(COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE OSGSOL_SCIENCE_PREVIEW_SDK
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE OSGSOL_SCIENCE_PREVIEW_XCRUN_RESULT)
IF(NOT OSGSOL_SCIENCE_PREVIEW_XCRUN_RESULT EQUAL 0)
    MESSAGE(FATAL_ERROR "xcrun could not locate the active macOS SDK")
ENDIF()

TARGET_LINK_LIBRARIES(osgSolSciencePreview
    "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libgdal.a"
    "${OSGSOL_SCIENCE_PREVIEW_SDK}/usr/lib/libcurl.tbd"
    "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libzstd.a"
    "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libproj.a"
    "${OSGSOL_SCIENCE_PREVIEW_SDK}/usr/lib/libsqlite3.tbd"
    m dl)

INSTALL(TARGETS osgSolSciencePreview
        ARCHIVE DESTINATION ${INSTALL_LIBDIR})

IF(BUILD_TESTING)
    ADD_EXECUTABLE(osgSol_Test_ScienceSqliteRtree
        ScienceSqliteRtreeTest.cpp)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceSqliteRtree
        PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty")
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceSqliteRtree
        PRIVATE osgVerseDependency)
    ADD_TEST(NAME osgSol_Test_ScienceSqliteRtree
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceSqliteRtree>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceSqliteRtree PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_SciencePreviewSmoke
        SciencePreviewSmokeTest.cpp)
    TARGET_LINK_LIBRARIES(osgSol_Test_SciencePreviewSmoke
        PRIVATE osgSolSciencePreview)
ENDIF()
