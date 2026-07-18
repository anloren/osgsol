ADD_LIBRARY(osgSolScienceCore STATIC
    CopernicusDemSource.cpp
    CopernicusDemSource.h
    ScienceAnalysisEngine.cpp
    ScienceAnalysisEngine.h
    ScienceAnalysisExport.cpp
    ScienceAnalysisExportCsv.cpp
    ScienceAnalysisExportDetail.h
    ScienceAnalysisExport.h
    ScienceLatentAnalysis.cpp
    ScienceAnalysisSupport.cpp
    ScienceAnalysisSupport.h
    ScienceEmbedding.cpp
    ScienceEmbedding.h
    ScienceEvidenceStore.cpp
    ScienceEvidenceStore.h
    ScienceProvider.h
    ScienceArtifactStore.cpp
    ScienceArtifactStore.h
    ScienceQueryService.cpp
    ScienceQueryService.h
    ScienceResearchTypes.cpp
    ScienceResearchTypes.h
    ScienceQueryTypes.cpp
    ScienceQueryTypes.h
    Sentinel2Stac.cpp
    Sentinel2Stac.h
    ScienceSourceRegistry.cpp
    ScienceSourceRegistry.h)

TARGET_COMPILE_FEATURES(osgSolScienceCore PUBLIC cxx_std_17)
TARGET_INCLUDE_DIRECTORIES(osgSolScienceCore
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty")
SET_TARGET_PROPERTIES(osgSolScienceCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden)

ADD_LIBRARY(osgSolSciencePreview STATIC
    AlphaEarthEmbeddingReader.cpp
    AlphaEarthEmbeddingReader.h
    AlphaEarthEmbeddingRuntime.cpp
    AlphaEarthEmbeddingRuntime.h
    AlphaEarthProvider.cpp
    AlphaEarthProvider.h
    CopernicusDemProvider.cpp
    CopernicusDemProvider.h
    CopernicusDemRuntime.cpp
    CopernicusDemRuntime.h
    Sentinel2Provider.cpp
    Sentinel2Provider.h
    Sentinel2Runtime.cpp
    Sentinel2Runtime.h
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

ADD_EXECUTABLE(osgSol_Smoke_Sentinel2Production EXCLUDE_FROM_ALL
    Sentinel2ProductionSmoke.cpp)
TARGET_COMPILE_FEATURES(osgSol_Smoke_Sentinel2Production PRIVATE cxx_std_17)
TARGET_LINK_LIBRARIES(osgSol_Smoke_Sentinel2Production
    PRIVATE osgSolSciencePreview)

ADD_EXECUTABLE(osgSol_Smoke_CopernicusDemProduction EXCLUDE_FROM_ALL
    CopernicusDemProductionSmoke.cpp)
TARGET_COMPILE_FEATURES(osgSol_Smoke_CopernicusDemProduction PRIVATE cxx_std_17)
TARGET_LINK_LIBRARIES(osgSol_Smoke_CopernicusDemProduction
    PRIVATE osgSolSciencePreview)

IF(BUILD_TESTING)
    ADD_EXECUTABLE(osgSol_Test_ScienceEvidenceStore
        ScienceEvidenceStoreTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceEvidenceStore PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceEvidenceStore
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceEvidenceStore
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceEvidenceStore>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceEvidenceStore PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_ScienceResearchTypes
        ScienceResearchTypesTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceResearchTypes PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceResearchTypes
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceResearchTypes
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceResearchTypes>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceResearchTypes PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_CopernicusDemProvider
        CopernicusDemProviderTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_CopernicusDemProvider PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_CopernicusDemProvider
        PRIVATE osgSolSciencePreview)
    ADD_TEST(NAME osgSol_Test_CopernicusDemProvider
             COMMAND $<TARGET_FILE:osgSol_Test_CopernicusDemProvider>)
    SET_TESTS_PROPERTIES(osgSol_Test_CopernicusDemProvider PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_CopernicusDemRuntime
        CopernicusDemRuntimeTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_CopernicusDemRuntime PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_CopernicusDemRuntime PRIVATE
        "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
    TARGET_LINK_LIBRARIES(osgSol_Test_CopernicusDemRuntime
        PRIVATE osgSolSciencePreview)
    ADD_TEST(NAME osgSol_Test_CopernicusDemRuntime
             COMMAND $<TARGET_FILE:osgSol_Test_CopernicusDemRuntime>)
    SET_TESTS_PROPERTIES(osgSol_Test_CopernicusDemRuntime PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_CopernicusDemSource
        CopernicusDemSourceTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_CopernicusDemSource PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_CopernicusDemSource
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_CopernicusDemSource
             COMMAND $<TARGET_FILE:osgSol_Test_CopernicusDemSource>)
    SET_TESTS_PROPERTIES(osgSol_Test_CopernicusDemSource PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_ScienceArtifactStore
        ScienceArtifactStoreTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceArtifactStore
        PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceArtifactStore
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceArtifactStore
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceArtifactStore>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceArtifactStore PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

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

    ADD_EXECUTABLE(osgSol_Test_Sentinel2Stac
        Sentinel2StacTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_Sentinel2Stac PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_Sentinel2Stac
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_Sentinel2Stac
             COMMAND $<TARGET_FILE:osgSol_Test_Sentinel2Stac>)
    SET_TESTS_PROPERTIES(osgSol_Test_Sentinel2Stac PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_Sentinel2Runtime
        Sentinel2RuntimeTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_Sentinel2Runtime PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_Sentinel2Runtime PRIVATE
        "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
    TARGET_LINK_LIBRARIES(osgSol_Test_Sentinel2Runtime
        PRIVATE osgSolSciencePreview)
    ADD_TEST(NAME osgSol_Test_Sentinel2Runtime
             COMMAND $<TARGET_FILE:osgSol_Test_Sentinel2Runtime>)
    SET_TESTS_PROPERTIES(osgSol_Test_Sentinel2Runtime PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_Sentinel2Provider
        Sentinel2ProviderTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_Sentinel2Provider PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_Sentinel2Provider
        PRIVATE osgSolSciencePreview)
    ADD_TEST(NAME osgSol_Test_Sentinel2Provider
             COMMAND $<TARGET_FILE:osgSol_Test_Sentinel2Provider>)
    SET_TESTS_PROPERTIES(osgSol_Test_Sentinel2Provider PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_ScienceEmbedding
        ScienceEmbeddingTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceEmbedding PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceEmbedding
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceEmbedding
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceEmbedding>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceEmbedding PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_ScienceAnalysisEngine
        ScienceAnalysisEngineTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceAnalysisEngine
        PRIVATE cxx_std_17)
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceAnalysisEngine
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceAnalysisEngine
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceAnalysisEngine>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceAnalysisEngine PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 10)

    ADD_EXECUTABLE(osgSol_Test_ScienceAnalysisExport
        ScienceAnalysisExportTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceAnalysisExport
        PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceAnalysisExport
        PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty")
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceAnalysisExport
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceAnalysisExport
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceAnalysisExport>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceAnalysisExport PROPERTIES
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

    ADD_EXECUTABLE(osgSol_Test_AlphaEarthEmbeddingRuntime
        AlphaEarthEmbeddingRuntimeTest.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_AlphaEarthEmbeddingRuntime
        PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_AlphaEarthEmbeddingRuntime
        PRIVATE "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
    TARGET_LINK_LIBRARIES(osgSol_Test_AlphaEarthEmbeddingRuntime
        PRIVATE osgSolSciencePreview)
    ADD_TEST(NAME osgSol_Test_AlphaEarthEmbeddingRuntime
             COMMAND $<TARGET_FILE:osgSol_Test_AlphaEarthEmbeddingRuntime>)
    SET_TESTS_PROPERTIES(osgSol_Test_AlphaEarthEmbeddingRuntime PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 30)

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
