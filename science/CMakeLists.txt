ADD_LIBRARY(osgSolScienceCore STATIC
    ScienceProvider.h
    ScienceQueryService.cpp
    ScienceQueryService.h
    ScienceQueryTypes.cpp
    ScienceQueryTypes.h
    ScienceSourceRegistry.cpp
    ScienceSourceRegistry.h)

TARGET_COMPILE_FEATURES(osgSolScienceCore PUBLIC cxx_std_17)
TARGET_INCLUDE_DIRECTORIES(osgSolScienceCore
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
SET_TARGET_PROPERTIES(osgSolScienceCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden)

ADD_LIBRARY(osgSolSciencePreview STATIC
    AlphaEarthProvider.cpp
    AlphaEarthProvider.h
    SciencePreviewSupport.cpp
    SciencePreviewSupport.h
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
    PUBLIC osgSolScienceCore
    PRIVATE
    "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libgdal.a"
    "${OSGSOL_SCIENCE_PREVIEW_SDK}/usr/lib/libcurl.tbd"
    "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libzstd.a"
    "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libproj.a"
    "${OSGSOL_SCIENCE_PREVIEW_SDK}/usr/lib/libsqlite3.tbd"
    m dl)

INSTALL(TARGETS osgSolScienceCore osgSolSciencePreview
        ARCHIVE DESTINATION ${INSTALL_LIBDIR})

IF(BUILD_TESTING)
    ADD_EXECUTABLE(osgSol_Test_ScienceQueryTypes
        ScienceQueryTypesTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceQueryTypes PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceQueryTypes
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceQueryTypes
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceQueryTypes>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceQueryTypes PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_ScienceSourceRegistry
        ScienceSourceRegistryTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceSourceRegistry
        PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceSourceRegistry
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceSourceRegistry
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceSourceRegistry>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceSourceRegistry PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_ScienceQueryService
        ScienceQueryServiceTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceQueryService
        PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceQueryService
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceQueryService
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceQueryService>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceQueryService PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_AlphaEarthProvider
        AlphaEarthProviderTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_AlphaEarthProvider
        PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_AlphaEarthProvider
        PRIVATE osgSolSciencePreview)
    ADD_TEST(NAME osgSol_Test_AlphaEarthProvider
             COMMAND $<TARGET_FILE:osgSol_Test_AlphaEarthProvider>)
    SET_TESTS_PROPERTIES(osgSol_Test_AlphaEarthProvider PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_SciencePreviewRegression
        SciencePreviewRegressionTest.cpp
        "${CMAKE_SOURCE_DIR}/applications/earth_explorer/science_preview_layer.cpp")
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_SciencePreviewRegression
        PRIVATE "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
    TARGET_LINK_LIBRARIES(osgSol_Test_SciencePreviewRegression
        osgSolSciencePreview osgVerseReaderWriter
        osgVersePipeline osgVerseModeling)
    LINK_OSG_LIBRARY(osgSol_Test_SciencePreviewRegression osg)
    ADD_TEST(NAME osgSol_Test_SciencePreviewRegression
             COMMAND $<TARGET_FILE:osgSol_Test_SciencePreviewRegression>)
    SET_TESTS_PROPERTIES(osgSol_Test_SciencePreviewRegression PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 30)

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
