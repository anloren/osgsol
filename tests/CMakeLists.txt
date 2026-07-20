MACRO(NEW_SIMPLE_EXECUTABLE EXECUTABLE_NAME IS_CUDA SOURCE_FILE)

    SET(EXE_NAME ${EXECUTABLE_NAME})
    SET(EXECUTABLE_FILES ${SOURCE_FILE})
    IF(${IS_CUDA})
        NEW_CUDA_EXECUTABLE(${EXE_NAME} SHARED)
    ELSE(${IS_CUDA})
        NEW_EXECUTABLE(${EXE_NAME} SHARED)
    ENDIF(${IS_CUDA})

    IF(VERSE_STATIC_BUILD)
        GET_PROPERTY(PLUGIN_LIBRARIES_TO_LINK GLOBAL PROPERTY VERSE_PLUGIN_LIBRARIES)
        SET(EXTERNAL_LIBRARIES ${EXTERNAL_LIBRARIES} ${PLUGIN_LIBRARIES_TO_LINK})

        IF(VERSE_USE_OSG_STATIC)
            SET(OSGPLUGIN_LIBRARIES osgdb_glsl osgdb_trans osgdb_rot osgdb_scale osgdb_osg osgdb_rgb osgdb_bmp)
            SET(OSGPLUGIN_LIBRARIES ${OSGPLUGIN_LIBRARIES}
                osgdb_deprecated_osg osgdb_deprecated_osgsim osgdb_deprecated_osgtext
                osgdb_deprecated_osgviewer osgdb_deprecated_osgterrain)
            SET(OSGPLUGIN_LIBRARIES ${OSGPLUGIN_LIBRARIES}
                osgdb_serializers_osg osgdb_serializers_osgtext osgdb_serializers_osgterrain osgdb_serializers_osgsim)
            RELINK_OSGPLUGINS(OSGPLUGIN_LIBRARIES2 ${OSGPLUGIN_LIBRARIES})
            SET(EXTERNAL_LIBRARIES ${EXTERNAL_LIBRARIES} ${OSGPLUGIN_LIBRARIES2})
        ENDIF(VERSE_USE_OSG_STATIC)
    ENDIF(VERSE_STATIC_BUILD)

    TARGET_COMPILE_OPTIONS(${EXE_NAME} PUBLIC -D_SCL_SECURE_NO_WARNINGS)
    TARGET_INCLUDE_DIRECTORIES(${EXE_NAME} PUBLIC ${EXTERNAL_INCLUDES})
    TARGET_LINK_LIBRARIES(${EXE_NAME} osgVerseDependency osgVerseReaderWriter osgVerseAnimation osgVersePipeline
                          osgVerseWrappers osgVerseModeling osgVerseScript osgVerseUI osgVerseAI
                          ${EXTERNAL_LIBRARIES})
    LINK_OSG_LIBRARY(${EXE_NAME} OpenThreads osg osgDB osgUtil osgGA osgText osgSim osgTerrain osgViewer)
    IF(NOT VERSE_STATIC_BUILD)
        IF(MSVC AND VERSE_INSTALL_PDB_FILES)
            INSTALL(FILES $<TARGET_PDB_FILE:${EXE_NAME}> DESTINATION ${INSTALL_BINDIR} OPTIONAL)
        ENDIF()
    ENDIF()

ENDMACRO(NEW_SIMPLE_EXECUTABLE)

MACRO(NEW_TEST EXECUTABLE_NAME SOURCE_FILE)
    NEW_SIMPLE_EXECUTABLE(${EXECUTABLE_NAME} FALSE ${SOURCE_FILE})
    SET_PROPERTY(TARGET ${EXECUTABLE_NAME} PROPERTY FOLDER "TESTS")
ENDMACRO(NEW_TEST)

MACRO(NEW_CTEST EXECUTABLE_NAME SOURCE_FILE TEST_LABEL TEST_TIMEOUT)
    NEW_TEST(${EXECUTABLE_NAME} ${SOURCE_FILE})
    IF(BUILD_TESTING)
        ADD_TEST(NAME ${EXECUTABLE_NAME} COMMAND $<TARGET_FILE:${EXECUTABLE_NAME}>)
        SET_TESTS_PROPERTIES(${EXECUTABLE_NAME} PROPERTIES
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
            LABELS "${TEST_LABEL}"
            TIMEOUT "${TEST_TIMEOUT}")
    ENDIF()
ENDMACRO(NEW_CTEST)

MACRO(NEW_EXAMPLE EXECUTABLE_NAME SOURCE_FILE)
    NEW_SIMPLE_EXECUTABLE(${EXECUTABLE_NAME} FALSE ${SOURCE_FILE})
    SET_PROPERTY(TARGET ${EXECUTABLE_NAME} PROPERTY FOLDER "EXAMPLES")
ENDMACRO(NEW_EXAMPLE)

MACRO(NEW_CUDA_EXAMPLE EXECUTABLE_NAME SOURCE_FILE)
    NEW_SIMPLE_EXECUTABLE(${EXECUTABLE_NAME} TRUE ${SOURCE_FILE})
    SET_PROPERTY(TARGET ${EXECUTABLE_NAME} PROPERTY FOLDER "EXAMPLES")
ENDMACRO(NEW_CUDA_EXAMPLE)

INCLUDE_DIRECTORIES(. ../3rdparty/libhv ../3rdparty/libhv/all)
ADD_DEFINITIONS(-DHV_STATICLIB)

IF(NOT VERSE_USE_EXTERNAL_GLES)
    NEW_TEST(osgVerse_Test_Compressing compressing_test.cpp)
    NEW_TEST(osgVerse_Test_Thread hybrid_thread_test.cpp)
    NEW_TEST(osgVerse_Test_Volume_Rendering volume_rendering_test.cpp)
    NEW_TEST(osgVerse_Test_Auto_LOD auto_lod_test.cpp)
    NEW_TEST(osgVerse_Test_Sky_Box sky_box_test.cpp)
    NEW_TEST(osgVerse_Test_Swig_Interface swig_interface_test.cpp)
    NEW_TEST(osgVerse_Test_Python_Server python_server_test.cpp)

    IF(OSG_MAJOR_VERSION GREATER 2 AND OSG_MINOR_VERSION GREATER 3)
        NEW_TEST(osgVerse_Test_Instance_Param instance_param_test.cpp)
        NEW_TEST(osgVerse_Test_Reserializing reserializing_test.cpp)
    ENDIF()
ENDIF(NOT VERSE_USE_EXTERNAL_GLES)

NEW_EXAMPLE(osgVerse_Test_Forward_Pbr forward_pbr_test.cpp)  # Forward PBR rendering pass example
NEW_EXAMPLE(osgVerse_Test_Plugins plugins_test.cpp)  # Auto-check dependencies and printing out example // FIXME: implement
NEW_EXAMPLE(osgVerse_Test_Pipeline pipeline_test.cpp)  # Basic pipeline usage and platform-testing example
NEW_EXAMPLE(osgVerse_Test_Report_Graph report_graph_test.cpp)  # Scene graph structure printer example
NEW_EXAMPLE(osgVerse_Test_Shader_Library shader_library_test.cpp)  # Shader library loading and platform-testing example
NEW_EXAMPLE(osgVerse_Test_Shadow shadow_test.cpp)  # Shadow algorithm and efficiency testing example
NEW_EXAMPLE(osgVerse_Test_Earth earth_test.cpp)  # Lightweight earth, atmosphere and ocean creation example
NEW_EXAMPLE(osgVerse_Test_3DGS gaussian_splatting_test.cpp)  # 3D gaussian splatting load and render example
NEW_EXAMPLE(osgVerse_Test_HUD_Text hud_text_test.cpp)  # HUD texts rendering and dynamic changing example
IF(ONNXRUNTIME_FOUND)
    NEW_EXAMPLE(osgVerse_Test_Onnx onnx_engine_test.cpp)  # ONNX-based AI model load and inference example
ENDIF(ONNXRUNTIME_FOUND)

IF(NOT VERSE_USE_EXTERNAL_GLES)
    NEW_EXAMPLE(osgVerse_Test_Pbr_Prerequisite pbr_prerequisite.cpp)  # PBR necessary textures creator example
    NEW_EXAMPLE(osgVerse_Test_Atmospheric atmospheric_scattering.cpp)  # Precomputed atmosphere textures example // FIXME: implement
    NEW_EXAMPLE(osgVerse_Test_CSG csg_stencil_test.cpp)  # Stencil-based CSG algorithm example  // FIXME: SCS implement, simple hint UI
    NEW_EXAMPLE(osgVerse_Test_ImGui imgui_test.cpp)  # Basic IMGUI-based UI example  // FIXME: add Drawer2D usage
    NEW_EXAMPLE(osgVerse_Test_MCP_Server mcp_server_test.cpp)  # Basic MCP server support example
    NEW_EXAMPLE(osgVerse_Test_Media_Stream media_stream_test.cpp)  # Media playing/streaming example  // FIXME: use datachannel, remove macro
    NEW_EXAMPLE(osgVerse_Test_Mesh_Process mesh_process_test.cpp)  # Basic mesh processing example  // FIXME: with simple hint UI
    NEW_EXAMPLE(osgVerse_Test_Navigation navigation_test.cpp)  # Mesh navigation example  // FIXME: with simple UI to add/set agents
    NEW_EXAMPLE(osgVerse_Test_Occlusion_Cull occlusion_cull_test.cpp)  # Soft-rasterizer based culling example  // FIXME: with huge data...
    NEW_EXAMPLE(osgVerse_Test_Paging_Lod paging_lod_test.cpp)  # Paging LOD optimization and creation example  // FIXME: with simple UI
    NEW_EXAMPLE(osgVerse_Test_Particle_U3D particle_u3d_test.cpp)  # Unity-like particle effect example  // FIXME: more effects
    NEW_EXAMPLE(osgVerse_Test_Particle_Cloud particle_cloud_test.cpp)  # Convert data to particle example  // FIXME: merge with last example
    NEW_EXAMPLE(osgVerse_Test_Player_Animation player_animation_test.cpp)  # Player animation example  // FIXME: with simple UI
    NEW_EXAMPLE(osgVerse_Test_Point_Cloud point_cloud_test.cpp)  # Point cloud with simple picking example  // FIXME: add eye-doming, UI
    NEW_EXAMPLE(osgVerse_Test_Polygon2D polygon2d_test.cpp)  # 2D polygon processing example  // FIXME: with simple hint UI
    NEW_EXAMPLE(osgVerse_Test_Symbols symbols_test.cpp)  # Massive symbol/label example  // FIXME: show on earth, with picking and UI
    NEW_EXAMPLE(osgVerse_Test_Tween_Animation tween_animation_test.cpp)  # Tween animation path example  // FIXME: with simple UI

    IF(CUDA_FOUND AND VERSE_BUILD_WITH_CUDA)
        NEW_EXAMPLE(osgVerse_Test_Video video_test.cpp)  # Hardware video encoding example
    ENDIF(CUDA_FOUND AND VERSE_BUILD_WITH_CUDA)

    IF(BULLET_FOUND)
        NEW_EXAMPLE(osgVerse_Test_Physics_Basic physics_basic_test.cpp)  # Basic rigid physics example  // FIXME: with simple hint UI
	    # TODO: physics_drawbridge_test, physics_softbody_test, player_walk_test
    ENDIF(BULLET_FOUND)

    IF(OSG_MAJOR_VERSION GREATER 2 AND OSG_MINOR_VERSION GREATER 5)
        NEW_EXAMPLE(osgVerse_Test_Scripting scripting_test.cpp)  # Script usage example  // FIXME: with simple hint UI
    ENDIF()

    IF(VERSE_SUPPORT_CPP17)
        SET(CMAKE_CXX_STANDARD 17)
        
        IF(EFFEKSEER_FOUND)
            NEW_EXAMPLE(osgVerse_Test_Particle_Effekseer particle_effekseer_test.cpp)  # Effekseer particle example  // FIXME: deprecate?
        ENDIF(EFFEKSEER_FOUND)
    ENDIF(VERSE_SUPPORT_CPP17)
ENDIF(NOT VERSE_USE_EXTERNAL_GLES)

NEW_CTEST(osgVerse_Test_Ai_Chat ai_chat_tests.cpp offline 120)  # AI Chat tool registry / job manager unit tests
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Ai_Chat PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_Feeds feed_layer_tests.cpp offline 120)  # FeedLayer framework / geo primitives / LayerManager unit tests
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Feeds PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_TileOverlay tile_overlay_tests.cpp offline 120)  # TileCallback overlay async placeholder texture (dark-tile regression)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_TileOverlay PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                           OSGVERSE_IMAGE_PLUGIN_PATH="$<TARGET_FILE:osgdb_verse_image>"
                           OSGVERSE_TMS_PLUGIN_PATH="$<TARGET_FILE:osgdb_verse_tms>")
ADD_DEPENDENCIES(osgVerse_Test_TileOverlay osgdb_verse_image osgdb_verse_tms)
NEW_CTEST(osgVerse_Test_TerrainGrid terrain_grid_tests.cpp offline 120)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_TerrainGrid PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_Tiles3dPaging tiles3d_paging_tests.cpp offline 120)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Tiles3dPaging PRIVATE
                           OSGVERSE_3DTILES_PLUGIN_PATH="$<TARGET_FILE:osgdb_verse_tiles>")
ADD_DEPENDENCIES(osgVerse_Test_Tiles3dPaging osgdb_verse_tiles)
NEW_CTEST(osgVerse_Test_Satellite satellite_tests.cpp offline 120)  # P2 satellite layer: SGP4 vendor + sat_math unit tests
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Satellite PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# Pure bbox / flight correctness seams
NEW_CTEST(osgVerse_Test_Geospatial geospatial_tests.cpp offline 120)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Geospatial PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_EarthManipulator earth_manipulator_tests.cpp offline 120)
NEW_CTEST(osgVerse_Test_Ais ais_tests.cpp offline 120)  # P3 AIS ship layer: message parse / bbox decision unit tests
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Ais PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_WorldTools world_tools_tests.cpp offline 120)  # P4 world-query tools: async fetcher / tool dispatch offline tests
NEW_CTEST(osgVerse_Test_ImGuiSettings imgui_settings_tests.cpp offline 30)
NEW_CTEST(osgVerse_Test_EarthControlLayout earth_control_layout_tests.cpp offline 30)
TARGET_INCLUDE_DIRECTORIES(osgVerse_Test_EarthControlLayout PRIVATE
                           "${CMAKE_SOURCE_DIR}/applications/earth_explorer")
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_EarthControlLayout PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_McpSafety mcp_safety_tests.cpp offline 30)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_McpSafety PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_ImGuiThreading imgui_threading_tests.cpp offline 30)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_ImGuiThreading PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_MediaThreading media_threading_tests.cpp offline 30)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_MediaThreading PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
NEW_CTEST(osgVerse_Test_OsgApplicationUsageExit osg_application_usage_exit_test.cpp
          "offline;macos-exit" 10)
NEW_CTEST(osgVerse_Test_TmsLayerLoadPool tms_layer_load_pool_tests.cpp
          "offline;macos-exit" 30)

IF(BUILD_TESTING)
    FIND_PROGRAM(OSGSOL_PYTHON3_EXECUTABLE NAMES python3)
    FIND_PROGRAM(OSGSOL_NODE_EXECUTABLE NAMES node)
    IF(OSGSOL_PYTHON3_EXECUTABLE)
        ADD_TEST(NAME osgSol_Test_AefIndexTool
                 COMMAND "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                         tests/aef_index_tool_tests.py -v)
        SET_TESTS_PROPERTIES(osgSol_Test_AefIndexTool PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "offline;scienceearth"
            TIMEOUT 30)
        ADD_TEST(NAME osgSol_Test_ScienceG0Manifest
                 COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                         "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                         tests.science_g0_manifest_tests -v)
        SET_TESTS_PROPERTIES(osgSol_Test_ScienceG0Manifest PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "offline;scienceearth"
            TIMEOUT 30)
        ADD_TEST(NAME osgSol_Test_TerrainScienceOverlayContract
                 COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                         "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                         tests.terrain_science_overlay_contract_tests -v)
        SET_TESTS_PROPERTIES(osgSol_Test_TerrainScienceOverlayContract PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "offline;scienceearth;terrain"
            TIMEOUT 30)
        ADD_TEST(NAME osgSol_Test_TerrainScienceShader
                 COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                         "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                         tests.terrain_science_shader_tests -v)
        SET_TESTS_PROPERTIES(osgSol_Test_TerrainScienceShader PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "offline;scienceearth;terrain"
            TIMEOUT 30)
        ADD_TEST(NAME osgSol_Test_MacosNormalExitGuard
                 COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                         "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                         tests.macos_normal_exit_guard_tests -v)
        SET_TESTS_PROPERTIES(osgSol_Test_MacosNormalExitGuard PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "offline;macos-exit"
            TIMEOUT 30)
        IF(APPLE)
            ADD_TEST(NAME osgSol_Test_RuntimePrefixRelocation
                     COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                             "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                             tests.runtime_prefix_relocation_tests -v)
            SET_TESTS_PROPERTIES(osgSol_Test_RuntimePrefixRelocation PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                LABELS "offline;scienceearth;packaging"
                TIMEOUT 30)
            ADD_TEST(NAME osgSol_Test_CompiledPathRelocation
                     COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                             "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                             tests.compiled_path_relocation_tests -v)
            SET_TESTS_PROPERTIES(osgSol_Test_CompiledPathRelocation PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                LABELS "offline;scienceearth;packaging"
                TIMEOUT 30)
        ENDIF()
    ENDIF()

    ADD_TEST(NAME osgSol_Test_ScienceDepsScript
             COMMAND bash "${CMAKE_SOURCE_DIR}/tests/science_deps_script_tests.sh")
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceDepsScript PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        LABELS "offline;scienceearth"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgVerse_Test_ScienceBuildContract science_build_contract_tests.cpp)
    TARGET_INCLUDE_DIRECTORIES(osgVerse_Test_ScienceBuildContract PRIVATE
                               "${CMAKE_BINARY_DIR}/generated")
    TARGET_COMPILE_DEFINITIONS(osgVerse_Test_ScienceBuildContract PRIVATE
                               OSGSOL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    SET_PROPERTY(TARGET osgVerse_Test_ScienceBuildContract PROPERTY FOLDER "TESTS")
    ADD_TEST(NAME osgVerse_Test_ScienceBuildContract
             COMMAND $<TARGET_FILE:osgVerse_Test_ScienceBuildContract>)
    SET_TESTS_PROPERTIES(osgVerse_Test_ScienceBuildContract PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 30)

    IF(APPLE)
        ADD_LIBRARY(osgSol_Test_SciencePluginFake MODULE
            science_plugin_fake.cpp)
        ADD_LIBRARY(osgSol_Test_SciencePluginFakeBadAbi MODULE
            science_plugin_fake_bad_abi.cpp)
        ADD_LIBRARY(osgSol_Test_SciencePluginFakeMissingAnchor MODULE
            science_plugin_fake_missing_anchor.cpp)
        ADD_LIBRARY(osgSol_Test_SciencePluginFakeNullFunction MODULE
            science_plugin_fake_null_function.cpp)
        FOREACH(_science_fake_target
                osgSol_Test_SciencePluginFake
                osgSol_Test_SciencePluginFakeBadAbi
                osgSol_Test_SciencePluginFakeMissingAnchor
                osgSol_Test_SciencePluginFakeNullFunction)
            SET_TARGET_PROPERTIES(${_science_fake_target} PROPERTIES
                PREFIX ""
                SUFFIX ".so"
                CXX_VISIBILITY_PRESET hidden)
        ENDFOREACH()

        ADD_EXECUTABLE(osgSol_Test_SciencePluginRuntime
            science_plugin_runtime_tests.cpp
            "${CMAKE_SOURCE_DIR}/applications/earth_explorer/science_plugin_runtime.cpp"
            "${CMAKE_SOURCE_DIR}/applications/earth_explorer/terrain_science_overlay.cpp"
            "${CMAKE_SOURCE_DIR}/applications/earth_explorer/terrain_science_overlay_math.cpp")
        TARGET_COMPILE_FEATURES(osgSol_Test_SciencePluginRuntime PRIVATE cxx_std_14)
        TARGET_INCLUDE_DIRECTORIES(osgSol_Test_SciencePluginRuntime PRIVATE
            "${CMAKE_SOURCE_DIR}/applications/earth_explorer")
        TARGET_COMPILE_DEFINITIONS(osgSol_Test_SciencePluginRuntime PRIVATE
            OSGSOL_TEST_SCIENCE_PLUGIN="$<TARGET_FILE:osgSol_Test_SciencePluginFake>"
            OSGSOL_TEST_SCIENCE_PLUGIN_BAD_ABI="$<TARGET_FILE:osgSol_Test_SciencePluginFakeBadAbi>"
            OSGSOL_TEST_SCIENCE_PLUGIN_MISSING_ANCHOR="$<TARGET_FILE:osgSol_Test_SciencePluginFakeMissingAnchor>"
            OSGSOL_TEST_SCIENCE_PLUGIN_NULL_FUNCTION="$<TARGET_FILE:osgSol_Test_SciencePluginFakeNullFunction>")
        ADD_DEPENDENCIES(osgSol_Test_SciencePluginRuntime
            osgSol_Test_SciencePluginFake
            osgSol_Test_SciencePluginFakeBadAbi
            osgSol_Test_SciencePluginFakeMissingAnchor
            osgSol_Test_SciencePluginFakeNullFunction)
        TARGET_LINK_LIBRARIES(osgSol_Test_SciencePluginRuntime PRIVATE
            ${CMAKE_DL_LIBS})
        LINK_OSG_LIBRARY(osgSol_Test_SciencePluginRuntime
            OpenThreads osg osgDB)
        ADD_TEST(NAME osgSol_Test_SciencePluginRuntime
                 COMMAND $<TARGET_FILE:osgSol_Test_SciencePluginRuntime>)
        SET_TESTS_PROPERTIES(osgSol_Test_SciencePluginRuntime PROPERTIES
            LABELS "offline;scienceearth"
            TIMEOUT 30)
    ENDIF()

    IF(OSGSOL_BUILD_SCIENCE)
        ADD_TEST(NAME osgSol_Test_ScienceQueryConsumer
                 COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                         "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                         tests.science_query_consumer_contract_tests -v)
        SET_TESTS_PROPERTIES(osgSol_Test_ScienceQueryConsumer PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "offline;scienceearth"
            TIMEOUT 30)

        IF(NOT EXISTS "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libgdal.a")
            MESSAGE(FATAL_ERROR
                "OSGSOL_SCIENCE_DEPS_ROOT must name the verified private static prefix")
        ENDIF()
        IF(NOT OSGSOL_PYTHON3_EXECUTABLE)
            MESSAGE(FATAL_ERROR "Python 3 is required for the instrumented range test")
        ENDIF()
        IF(NOT OSGSOL_NODE_EXECUTABLE)
            MESSAGE(FATAL_ERROR "Node.js is required for the local HTTP/2 range test")
        ENDIF()

        IF(NOT APPLE)
            MESSAGE(FATAL_ERROR
                "The verified Task 3 private dependency prefix currently targets macOS")
        ENDIF()
        EXECUTE_PROCESS(COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE OSGSOL_SCIENCE_SDK_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE OSGSOL_SCIENCE_XCRUN_RESULT)
        IF(NOT OSGSOL_SCIENCE_XCRUN_RESULT EQUAL 0)
            MESSAGE(FATAL_ERROR "xcrun could not locate the active macOS SDK")
        ENDIF()
        SET(_OSGSOL_SCIENCE_GDAL_ARCHIVE
            "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libgdal.a")
        SET(_OSGSOL_SCIENCE_PROJ_ARCHIVE
            "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libproj.a")
        SET(_OSGSOL_SCIENCE_ZSTD_ARCHIVE
            "${OSGSOL_SCIENCE_DEPS_ROOT}/lib/libzstd.a")
        SET(_OSGSOL_SCIENCE_CURL_STUB
            "${OSGSOL_SCIENCE_SDK_ROOT}/usr/lib/libcurl.tbd")
        SET(_OSGSOL_SCIENCE_SQLITE_STUB
            "${OSGSOL_SCIENCE_SDK_ROOT}/usr/lib/libsqlite3.tbd")
        FOREACH(_OSGSOL_SCIENCE_LIBRARY
                ${_OSGSOL_SCIENCE_GDAL_ARCHIVE}
                ${_OSGSOL_SCIENCE_PROJ_ARCHIVE}
                ${_OSGSOL_SCIENCE_ZSTD_ARCHIVE}
                ${_OSGSOL_SCIENCE_CURL_STUB}
                ${_OSGSOL_SCIENCE_SQLITE_STUB})
            IF(NOT EXISTS "${_OSGSOL_SCIENCE_LIBRARY}")
                MESSAGE(FATAL_ERROR
                    "ScienceEarth test dependency is missing: ${_OSGSOL_SCIENCE_LIBRARY}")
            ENDIF()
        ENDFOREACH()
        ADD_LIBRARY(osgSolScienceGdalTestDeps INTERFACE)
        TARGET_INCLUDE_DIRECTORIES(osgSolScienceGdalTestDeps INTERFACE
                                   "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
        TARGET_LINK_LIBRARIES(osgSolScienceGdalTestDeps INTERFACE
            "${_OSGSOL_SCIENCE_GDAL_ARCHIVE}"
            "${_OSGSOL_SCIENCE_CURL_STUB}"
            "${_OSGSOL_SCIENCE_ZSTD_ARCHIVE}"
            "${_OSGSOL_SCIENCE_PROJ_ARCHIVE}"
            m dl "${_OSGSOL_SCIENCE_SQLITE_STUB}")

        ADD_EXECUTABLE(osgVerse_Test_ScienceGdalSpike science_gdal_spike.cpp)
        TARGET_COMPILE_FEATURES(osgVerse_Test_ScienceGdalSpike PRIVATE cxx_std_17)
        TARGET_INCLUDE_DIRECTORIES(osgVerse_Test_ScienceGdalSpike PRIVATE
                                   "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
        TARGET_LINK_LIBRARIES(osgVerse_Test_ScienceGdalSpike PRIVATE
                              osgSolScienceGdalTestDeps)
        SET_PROPERTY(TARGET osgVerse_Test_ScienceGdalSpike PROPERTY FOLDER "TESTS")
        ADD_TEST(NAME osgVerse_Test_ScienceGdalSpike
                 COMMAND $<TARGET_FILE:osgVerse_Test_ScienceGdalSpike>)
        SET_TESTS_PROPERTIES(osgVerse_Test_ScienceGdalSpike PROPERTIES
            LABELS "offline;scienceearth"
            TIMEOUT 120)

        ADD_EXECUTABLE(osgVerse_Test_ScienceHttpRanges science_gdal_network_test.cpp)
        TARGET_COMPILE_FEATURES(osgVerse_Test_ScienceHttpRanges PRIVATE cxx_std_17)
        TARGET_COMPILE_DEFINITIONS(osgVerse_Test_ScienceHttpRanges PRIVATE
            OSGSOL_PYTHON3="${OSGSOL_PYTHON3_EXECUTABLE}"
            OSGSOL_NODE="${OSGSOL_NODE_EXECUTABLE}"
            OSGSOL_SCIENCE_RANGE_SERVER="${CMAKE_CURRENT_SOURCE_DIR}/science_http_range_server.py"
            OSGSOL_SCIENCE_HTTP2_RANGE_SERVER="${CMAKE_CURRENT_SOURCE_DIR}/science_http2_range_server.mjs"
            OSGSOL_SCIENCE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/science"
            OSGSOL_SCIENCE_EVIDENCE_DIR="${CMAKE_BINARY_DIR}/science-network-evidence"
            OSGSOL_SCIENCE_BUILD_DIR="${CMAKE_BINARY_DIR}"
            OSGSOL_SCIENCE_PROTECTED_EVIDENCE_DIR="${PROJECT_SOURCE_DIR}/build/science_g0/science-network-evidence")
        TARGET_INCLUDE_DIRECTORIES(osgVerse_Test_ScienceHttpRanges PRIVATE
                                   "${PROJECT_SOURCE_DIR}/3rdparty")
        TARGET_LINK_LIBRARIES(osgVerse_Test_ScienceHttpRanges PRIVATE
                              osgSolScienceGdalTestDeps)
        IF(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
            ADD_LIBRARY(osgSol_Test_CurlRemoveFault SHARED
                        science_curl_remove_fault.cpp)
            TARGET_COMPILE_FEATURES(osgSol_Test_CurlRemoveFault PRIVATE cxx_std_17)
            TARGET_LINK_LIBRARIES(osgSol_Test_CurlRemoveFault PRIVATE
                                  "${_OSGSOL_SCIENCE_CURL_STUB}")
            SET_PROPERTY(TARGET osgSol_Test_CurlRemoveFault PROPERTY FOLDER "TESTS")
            ADD_DEPENDENCIES(osgVerse_Test_ScienceHttpRanges
                             osgSol_Test_CurlRemoveFault)
        ENDIF()
        SET_PROPERTY(TARGET osgVerse_Test_ScienceHttpRanges PROPERTY FOLDER "TESTS")
        ADD_TEST(NAME osgVerse_Test_ScienceHttpRanges
                 COMMAND $<TARGET_FILE:osgVerse_Test_ScienceHttpRanges>)
        SET_TESTS_PROPERTIES(osgVerse_Test_ScienceHttpRanges PROPERTIES
            LABELS "offline;scienceearth;network-local"
            TIMEOUT 120)
        IF(APPLE)
            SET_PROPERTY(TEST osgVerse_Test_ScienceHttpRanges APPEND PROPERTY
                ENVIRONMENT
                "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:osgSol_Test_CurlRemoveFault>")
        ELSEIF(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            SET_PROPERTY(TEST osgVerse_Test_ScienceHttpRanges APPEND PROPERTY
                ENVIRONMENT
                "LD_PRELOAD=$<TARGET_FILE:osgSol_Test_CurlRemoveFault>")
        ENDIF()
        ADD_TEST(NAME osgVerse_Test_ScienceHttpRangesCoordinatorAbandonment
                 COMMAND $<TARGET_FILE:osgVerse_Test_ScienceHttpRanges>)
        SET(_OSGSOL_COORDINATOR_ABANDONMENT_ENV
            "OSGSOL_TEST_PREFETCH_CASE=success-remove-failure")
        LIST(APPEND _OSGSOL_COORDINATOR_ABANDONMENT_ENV
            "OSGSOL_TEST_MULTIRANGE_CASE=global-only")
        SET_TESTS_PROPERTIES(
            osgVerse_Test_ScienceHttpRangesCoordinatorAbandonment PROPERTIES
            LABELS "offline;scienceearth;network-local"
            TIMEOUT 120
            ENVIRONMENT "${_OSGSOL_COORDINATOR_ABANDONMENT_ENV}")
        UNSET(_OSGSOL_COORDINATOR_ABANDONMENT_ENV)
        IF(APPLE)
            SET_PROPERTY(
                TEST osgVerse_Test_ScienceHttpRangesCoordinatorAbandonment
                APPEND PROPERTY ENVIRONMENT
                "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:osgSol_Test_CurlRemoveFault>")
        ELSEIF(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            SET_PROPERTY(
                TEST osgVerse_Test_ScienceHttpRangesCoordinatorAbandonment
                APPEND PROPERTY ENVIRONMENT
                "LD_PRELOAD=$<TARGET_FILE:osgSol_Test_CurlRemoveFault>")
        ENDIF()
        ADD_TEST(NAME osgVerse_Test_ScienceHttpRangesBlockedOperationCapacity
                 COMMAND $<TARGET_FILE:osgVerse_Test_ScienceHttpRanges>)
        SET(_OSGSOL_BLOCKED_OPERATION_CAPACITY_ENV
            "OSGSOL_TEST_PREFETCH_CASE=blocked-operation-capacity")
        LIST(APPEND _OSGSOL_BLOCKED_OPERATION_CAPACITY_ENV
            "OSGSOL_TEST_MULTIRANGE_CASE=global-only")
        SET_TESTS_PROPERTIES(
            osgVerse_Test_ScienceHttpRangesBlockedOperationCapacity PROPERTIES
            LABELS "offline;scienceearth;network-local"
            TIMEOUT 180
            ENVIRONMENT "${_OSGSOL_BLOCKED_OPERATION_CAPACITY_ENV}")
        UNSET(_OSGSOL_BLOCKED_OPERATION_CAPACITY_ENV)
        IF(APPLE)
            SET_PROPERTY(
                TEST osgVerse_Test_ScienceHttpRangesBlockedOperationCapacity
                APPEND PROPERTY ENVIRONMENT
                "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:osgSol_Test_CurlRemoveFault>")
        ELSEIF(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            SET_PROPERTY(
                TEST osgVerse_Test_ScienceHttpRangesBlockedOperationCapacity
                APPEND PROPERTY ENVIRONMENT
                "LD_PRELOAD=$<TARGET_FILE:osgSol_Test_CurlRemoveFault>")
        ENDIF()
        ADD_TEST(NAME osgSol_Test_ScienceHttpRangeServer
            COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                    "${OSGSOL_PYTHON3_EXECUTABLE}" -m unittest
                    tests.science_http_range_server_tests -v)
        SET_TESTS_PROPERTIES(osgSol_Test_ScienceHttpRangeServer PROPERTIES
            LABELS "offline;scienceearth;network-local"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            TIMEOUT 30)
        IF(OSGSOL_SCIENCE_NETWORK_TESTS)
            ADD_TEST(NAME osgVerse_Test_ScienceGdalLive
                COMMAND $<TARGET_FILE:osgVerse_Test_ScienceHttpRanges>
                        --live-cases
                        "${CMAKE_CURRENT_SOURCE_DIR}/data/science/alphaearth_rgb_cases.json"
                        --iterations 5
                        --profile prefetch
                        --evidence-dir
                        "${CMAKE_BINARY_DIR}/formal-evidence-v4"
                        --summary-json
                        "${CMAKE_BINARY_DIR}/formal-evidence-v4/live-summary.json"
                        --enforce-latency)
            SET_TESTS_PROPERTIES(osgVerse_Test_ScienceGdalLive PROPERTIES
                LABELS "network;scienceearth"
                TIMEOUT 300)
        ENDIF()

        ADD_LIBRARY(osgdb_science_g0_probe MODULE science_gdal_probe_plugin.cpp)
        TARGET_COMPILE_FEATURES(osgdb_science_g0_probe PRIVATE cxx_std_17)
        TARGET_LINK_LIBRARIES(osgdb_science_g0_probe PRIVATE osgSolScienceGdalTestDeps)
        SET_TARGET_PROPERTIES(osgdb_science_g0_probe PROPERTIES
            PREFIX ""
            SUFFIX ".so"
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            SKIP_BUILD_RPATH ON
            BUILD_WITH_INSTALL_RPATH OFF
            INSTALL_RPATH ""
            LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
        IF(APPLE)
            IF(COMMAND TARGET_LINK_OPTIONS)
                TARGET_LINK_OPTIONS(osgdb_science_g0_probe PRIVATE
                    "-Wl,-dead_strip"
                    "-Wl,-exported_symbol,_osgsol_science_g0_probe_anchor")
            ELSE()
                SET_PROPERTY(TARGET osgdb_science_g0_probe APPEND_STRING PROPERTY
                    LINK_FLAGS
                    " -Wl,-dead_strip -Wl,-exported_symbol,_osgsol_science_g0_probe_anchor")
            ENDIF()
        ELSEIF(UNIX)
            IF(COMMAND TARGET_LINK_OPTIONS)
                TARGET_LINK_OPTIONS(osgdb_science_g0_probe PRIVATE
                                    "-Wl,--gc-sections" "-Wl,--exclude-libs,ALL")
            ELSE()
                SET_PROPERTY(TARGET osgdb_science_g0_probe APPEND_STRING PROPERTY
                    LINK_FLAGS " -Wl,--gc-sections -Wl,--exclude-libs,ALL")
            ENDIF()
        ENDIF()
        SET_PROPERTY(TARGET osgdb_science_g0_probe PROPERTY FOLDER "TESTS")
    ENDIF()
ENDIF()

IF(VERSE_WITH_COMMON_LIBRARIES)
    NEW_CTEST(osgVerse_Test_InputSafety input_safety_tests.cpp offline 30)
    TARGET_LINK_LIBRARIES(osgVerse_Test_InputSafety osgdb_verse_web zlib)
ENDIF(VERSE_WITH_COMMON_LIBRARIES)

IF(VERSE_BUILD_DEPRECATED_TESTS)
    IF(NOT VERSE_USE_EXTERNAL_GLES)
        NEW_TEST(osgVerse_Test_FastRtt deprecated/fast_rtt_test.cpp)
        NEW_TEST(osgVerse_Test_CubeRtt deprecated/render_to_cube_test.cpp)
        NEW_TEST(osgVerse_Test_Obb_KDop deprecated/obb_kdop_test.cpp)
        NEW_TEST(osgVerse_Test_Mesh_Boolean deprecated/mesh_boolean_test.cpp)
        NEW_TEST(osgVerse_Test_Auto_Imposter deprecated/auto_imposter_test.cpp)
        NEW_TEST(osgVerse_Test_Texture_Mapping deprecated/texture_mapping_test.cpp)
	    IF(MSVC_VERSION GREATER 1900)
            NEW_TEST(osgVerse_Test_Restful_Server deprecated/restful_server_test.cpp)
	    ENDIF(MSVC_VERSION GREATER 1900)

        IF(OSG_MAJOR_VERSION GREATER 2 AND OSG_MINOR_VERSION GREATER 5)
            NEW_TEST(osgVerse_Test_Indirect_Draw deprecated/indirect_drawing_test.cpp)
            NEW_TEST(osgVerse_Test_Tessellation deprecated/tessellation_test.cpp)
            NEW_TEST(osgVerse_Test_MultiView_Shader deprecated/multiview_shader_test.cpp)
        ENDIF()
    ENDIF(NOT VERSE_USE_EXTERNAL_GLES)
ENDIF(VERSE_BUILD_DEPRECATED_TESTS)
