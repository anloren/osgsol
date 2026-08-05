SET(EXE_NAME osgVerse_EarthExplorer)
SET(EXECUTABLE_FILES
    earth_main.cpp ui.cpp earth_ui_v2.cpp earth_ui_v2.h earth_ui_chart.h
    render_effects.cpp render_effects.h
    city_data.cpp ReaderWriterCSV.cpp precip_data.cpp flight_data.cpp flight_math.cpp
    tiles3d_data.cpp sat_math.cpp sat_data.cpp ais_math.cpp ais_data.cpp
    earth_config.cpp marker_style.cpp
    ai_chat.cpp ai_setup.cpp ai_ui.cpp ai_cards.cpp ai_media.cpp ai_query.cpp ai_world_tools.cpp
    earth_context.cpp earth_context.h
    project/earth_project.cpp project/earth_project.h
    project/earth_project_command_bus.cpp project/earth_project_command_bus.h
    project/earth_project_command_context.cpp
    project/earth_project_command_context.h
    project/earth_temporal_controller.cpp
    project/earth_temporal_controller.h
    project/earth_project_layer_adapter.cpp
    project/earth_project_layer_adapter.h
    feed_layer.cpp feeds/gdacs_feed.cpp feeds/usgs_quakes_feed.cpp feeds/eonet_feed.cpp
    feeds/gdelt_feed.cpp feeds/gpsjam_feed.cpp feeds/unhcr_feed.cpp feeds/nhc_feed.cpp
    feeds/strategic_feed.cpp feeds/firms_feed.cpp
    geo_primitives.cpp
)

IF(OSGSOL_BUILD_SCIENCE)
    LIST(APPEND EXECUTABLE_FILES
         science_plugin_runtime.cpp science_plugin_runtime.h
         science_plugin_api.h
         science_ui/science_target_overlay.cpp
         science_ui/science_target_overlay.h
         terrain_science_overlay.cpp terrain_science_overlay.h
         terrain_science_overlay_math.cpp terrain_science_overlay_math.h)
ENDIF()

IF(OSGSOL_BUILD_RMLUI_PRODUCT_UI)
    # RmlUi's stock GL3 backend always composites its final layer into FBO 0.
    # osgVerse renders cameras into whichever framebuffer is active, so FBO 0
    # can be the wrong target (and is invalid for our no-window CGL verifier).
    # Generate a narrowly patched backend which remembers the draw framebuffer
    # active at BeginFrame and composites back into that same target.
    SET(OSGSOL_RMLUI_GL3_PATCH_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/generated/rmlui_gl3")
    FILE(MAKE_DIRECTORY "${OSGSOL_RMLUI_GL3_PATCH_DIR}")
    FILE(READ "${OSGSOL_RMLUI_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.h"
         OSGSOL_RMLUI_GL3_HEADER)
    STRING(REPLACE
        "int viewport_width = 0;"
        "unsigned int target_framebuffer = 0;\n\n\tint viewport_width = 0;"
        OSGSOL_RMLUI_GL3_HEADER_PATCHED "${OSGSOL_RMLUI_GL3_HEADER}")
    IF(OSGSOL_RMLUI_GL3_HEADER_PATCHED STREQUAL OSGSOL_RMLUI_GL3_HEADER)
        MESSAGE(FATAL_ERROR "RmlUi GL3 header patch anchor was not found")
    ENDIF()
    FILE(WRITE "${OSGSOL_RMLUI_GL3_PATCH_DIR}/RmlUi_Renderer_GL3.h"
         "${OSGSOL_RMLUI_GL3_HEADER_PATCHED}")
    FILE(READ "${OSGSOL_RMLUI_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp"
         OSGSOL_RMLUI_GL3_SOURCE)
    STRING(REPLACE
        "void RenderInterface_GL3::BeginFrame()\n{\n\tRMLUI_ASSERT(viewport_width >= 1 && viewport_height >= 1);"
        "void RenderInterface_GL3::BeginFrame()\n{\n\tRMLUI_ASSERT(viewport_width >= 1 && viewport_height >= 1);\n\n\tGLint active_framebuffer = 0;\n\tglGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &active_framebuffer);\n\ttarget_framebuffer = static_cast<GLuint>(active_framebuffer);"
        OSGSOL_RMLUI_GL3_SOURCE_PATCHED "${OSGSOL_RMLUI_GL3_SOURCE}")
    IF(OSGSOL_RMLUI_GL3_SOURCE_PATCHED STREQUAL OSGSOL_RMLUI_GL3_SOURCE)
        MESSAGE(FATAL_ERROR "RmlUi GL3 BeginFrame patch anchor was not found")
    ENDIF()
    STRING(REPLACE
        "// Draw to backbuffer\n\tglBindFramebuffer(GL_FRAMEBUFFER, 0);"
        "// Draw to the framebuffer active when BeginFrame was called.\n\tglBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer);"
        OSGSOL_RMLUI_GL3_SOURCE_PATCHED_2
        "${OSGSOL_RMLUI_GL3_SOURCE_PATCHED}")
    IF(OSGSOL_RMLUI_GL3_SOURCE_PATCHED_2 STREQUAL
       OSGSOL_RMLUI_GL3_SOURCE_PATCHED)
        MESSAGE(FATAL_ERROR "RmlUi GL3 EndFrame patch anchor was not found")
    ENDIF()
    FILE(WRITE "${OSGSOL_RMLUI_GL3_PATCH_DIR}/RmlUi_Renderer_GL3.cpp"
         "${OSGSOL_RMLUI_GL3_SOURCE_PATCHED_2}")

    LIST(APPEND EXECUTABLE_FILES
         product_ui/rml_input_bridge.cpp product_ui/rml_input_bridge.h
         product_ui/rml_osg_renderer.cpp product_ui/rml_osg_renderer.h
         product_ui/rml_ui_runtime.cpp product_ui/rml_ui_runtime.h
         "${OSGSOL_RMLUI_GL3_PATCH_DIR}/RmlUi_Renderer_GL3.cpp"
         "${OSGSOL_RMLUI_GL3_PATCH_DIR}/RmlUi_Renderer_GL3.h"
         "${OSGSOL_RMLUI_SOURCE_DIR}/Backends/RmlUi_Include_GL3.h")
    IF(OSGSOL_BUILD_SCIENCE)
        LIST(APPEND EXECUTABLE_FILES
             science_ui/science_chart_model.cpp
             science_ui/science_chart_model.h
             science_ui/science_product_ui_selector.cpp
             science_ui/science_product_ui_selector.h
             science_ui/rml_science_chart.cpp
             science_ui/rml_science_chart.h
             science_ui/map_context_capture.cpp
             science_ui/map_context_capture.h
             science_ui/science_report_window.cpp
             science_ui/science_report_window.h
             science_ui/science_workbench_presenter.cpp
             science_ui/science_workbench_presenter.h)
    ENDIF()
ENDIF()

IF(APPLE)
    # 中文 IME 直打桥(Objective-C++,给 OSG Cocoa 窗口挂 NSTextInputClient overlay,
    # 设计说明见 ime_bridge.h)。不能在子目录 ENABLE_LANGUAGE(OBJCXX)(CMake 语言
    # 信息不向兄弟目录传播,报 CMAKE_OBJCXX_COMPILE_OBJECT 缺失),改为按 CXX 规则
    # 编译 + "-x objective-c++" 强制 clang 走 ObjC++ 前端,零全局影响。
    SET_SOURCE_FILES_PROPERTIES(ime_bridge.mm PROPERTIES
                                LANGUAGE CXX COMPILE_FLAGS "-x objective-c++")
    SET_SOURCE_FILES_PROPERTIES(cinematic_video_encoder.mm PROPERTIES
                                LANGUAGE CXX COMPILE_FLAGS "-x objective-c++")
    SET(EXECUTABLE_FILES ${EXECUTABLE_FILES} ime_bridge.mm
                         cinematic_video_encoder.mm cinematic_video_encoder.h)
    IF(OSGSOL_BUILD_RMLUI_PRODUCT_UI)
        SET_SOURCE_FILES_PROPERTIES(product_ui/rml_macos_ime.mm PROPERTIES
                                    LANGUAGE CXX COMPILE_FLAGS "-x objective-c++")
        LIST(APPEND EXECUTABLE_FILES
             product_ui/rml_macos_ime.mm product_ui/rml_macos_ime.h)
    ENDIF()
ELSE()
    LIST(APPEND EXECUTABLE_FILES
         cinematic_video_encoder.cpp cinematic_video_encoder.h)
ENDIF(APPLE)

# 静态战略数据集(P1 T7):vendored JSON 随构建拷到 <install>/models/Earth/strategic/,
# 运行时按 mainFolder 派生的绝对路径读取(packaging/package_macos.sh 已整目录拷 models)。
ADD_CUSTOM_TARGET(EarthStrategicData ALL COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "${CMAKE_CURRENT_SOURCE_DIR}/data/strategic"
                  "${CMAKE_INSTALL_PREFIX}/models/Earth/strategic")
SET_PROPERTY(TARGET EarthStrategicData PROPERTY FOLDER "RESOURCES")
ADD_DEFINITIONS(-DHV_STATICLIB)
INCLUDE_DIRECTORIES(../../3rdparty/libhv ../../3rdparty/libhv/all)

NEW_EXECUTABLE(${EXE_NAME} SHARED)
SET_PROPERTY(TARGET ${EXE_NAME} PROPERTY FOLDER "APPLICATIONS")
TARGET_LINK_LIBRARIES(${EXE_NAME} osgVerseReaderWriter osgVerseAnimation osgVersePipeline
                      osgVerseWrappers osgVerseModeling osgVerseScript osgVerseUI osgVerseAI
                      osgVerseDependency ${EXTERNAL_LIBRARIES})
IF(OSGSOL_BUILD_SCIENCE)
    TARGET_LINK_LIBRARIES(${EXE_NAME} ${CMAKE_DL_LIBS})
    TARGET_COMPILE_DEFINITIONS(${EXE_NAME} PRIVATE OSGSOL_BUILD_SCIENCE=1)
ELSE()
    TARGET_COMPILE_DEFINITIONS(${EXE_NAME} PRIVATE OSGSOL_BUILD_SCIENCE=0)
ENDIF()
IF(OSGSOL_BUILD_RMLUI_PRODUCT_UI)
    TARGET_COMPILE_FEATURES(${EXE_NAME} PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(${EXE_NAME} PRIVATE
        "${OSGSOL_RMLUI_GL3_PATCH_DIR}"
        "${OSGSOL_RMLUI_SOURCE_DIR}/Backends")
    TARGET_LINK_LIBRARIES(${EXE_NAME} RmlUi::RmlUi ${CMAKE_DL_LIBS})
    TARGET_COMPILE_DEFINITIONS(${EXE_NAME} PRIVATE
        OSGSOL_BUILD_RMLUI_PRODUCT_UI=1
        OSGSOL_PRODUCT_UI_DEFAULT="${OSGSOL_PRODUCT_UI}")
ELSE()
    TARGET_COMPILE_DEFINITIONS(${EXE_NAME} PRIVATE
        OSGSOL_BUILD_RMLUI_PRODUCT_UI=0
        OSGSOL_PRODUCT_UI_DEFAULT="legacy")
ENDIF()
LINK_OSG_LIBRARY(${EXE_NAME} OpenThreads osg osgDB osgUtil osgGA osgText osgSim osgTerrain osgViewer)
TARGET_COMPILE_OPTIONS(${EXE_NAME} PUBLIC -D_SCL_SECURE_NO_WARNINGS)
IF(APPLE)
    TARGET_LINK_LIBRARIES(${EXE_NAME}
        "-framework Cocoa"   # ime_bridge.mm 的 AppKit 依赖
        "-framework Foundation"
        "-framework CoreFoundation"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreGraphics"
        "-framework ImageIO")
ENDIF(APPLE)

IF(APPLE AND OSGSOL_BUILD_SCIENCE AND BUILD_TESTING)
    FIND_PACKAGE(Python3 COMPONENTS Interpreter REQUIRED)
    ADD_TEST(NAME osgSol_Test_SciencePluginContract
        COMMAND ${CMAKE_COMMAND} -E env
            PYTHONDONTWRITEBYTECODE=1
            OSGSOL_SCIENCE_PLUGIN=$<TARGET_FILE:osgdb_science>
            OSGSOL_SCIENCE_MAIN=$<TARGET_FILE:${EXE_NAME}>
            OSGSOL_SCIENCE_MAIN_LINK_FILE=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${EXE_NAME}.dir/link.txt
            ${Python3_EXECUTABLE} -m unittest
            tests.science_plugin_contract_tests -v)
    SET_TESTS_PROPERTIES(osgSol_Test_SciencePluginContract PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        LABELS "offline;scienceearth"
        TIMEOUT 30)
ENDIF()

IF(OSGSOL_BUILD_SCIENCE AND BUILD_TESTING)
    ADD_EXECUTABLE(osgSol_Test_ScienceWorkbenchUiContract
        "${CMAKE_SOURCE_DIR}/tests/science_workbench_ui_contract_tests.cpp")
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceWorkbenchUiContract PRIVATE
        cxx_std_17)
    TARGET_COMPILE_DEFINITIONS(osgSol_Test_ScienceWorkbenchUiContract PRIVATE
        OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    ADD_TEST(NAME osgSol_Test_ScienceWorkbenchUiContract
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceWorkbenchUiContract>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceWorkbenchUiContract PROPERTIES
        LABELS "offline;scienceearth;ui"
        TIMEOUT 30)

    IF(OSGSOL_BUILD_RMLUI_PRODUCT_UI)
        ADD_EXECUTABLE(osgSol_Test_ScienceEarthOffline
            "${CMAKE_SOURCE_DIR}/tests/science_earth_offline_tests.cpp"
            science_ui/science_product_ui_selector.cpp)
        TARGET_COMPILE_FEATURES(osgSol_Test_ScienceEarthOffline PRIVATE
            cxx_std_17)
        TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceEarthOffline PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}")
        ADD_TEST(NAME osgSol_Test_ScienceEarthOffline
                 COMMAND $<TARGET_FILE:osgSol_Test_ScienceEarthOffline>)
        SET_TESTS_PROPERTIES(osgSol_Test_ScienceEarthOffline PROPERTIES
            LABELS "offline;scienceearth;ui"
            TIMEOUT 30)

        ADD_EXECUTABLE(osgSol_Test_RmlDocumentSmoke
            "${CMAKE_SOURCE_DIR}/tests/rml_document_smoke_tests.cpp"
            science_ui/rml_science_chart.cpp
            science_ui/science_chart_model.cpp)
        TARGET_COMPILE_FEATURES(osgSol_Test_RmlDocumentSmoke PRIVATE
            cxx_std_17)
        TARGET_COMPILE_DEFINITIONS(osgSol_Test_RmlDocumentSmoke PRIVATE
            OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        TARGET_INCLUDE_DIRECTORIES(osgSol_Test_RmlDocumentSmoke PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}")
        TARGET_LINK_LIBRARIES(osgSol_Test_RmlDocumentSmoke PRIVATE
            RmlUi::RmlUi png zlib)
        ADD_TEST(NAME osgSol_Test_RmlDocumentSmoke
                 COMMAND $<TARGET_FILE:osgSol_Test_RmlDocumentSmoke>)
        SET_TESTS_PROPERTIES(osgSol_Test_RmlDocumentSmoke PROPERTIES
            LABELS "offline;scienceearth;ui"
            TIMEOUT 30)

        IF(APPLE)
            ADD_EXECUTABLE(osgSol_Test_RmlGlRuntimeSmoke
                "${CMAKE_SOURCE_DIR}/tests/rml_gl_runtime_smoke_tests.cpp"
                ai_ui.cpp
                earth_ui_v2.cpp
                marker_style.cpp
                product_ui/rml_input_bridge.cpp
                product_ui/rml_osg_renderer.cpp
                product_ui/rml_ui_runtime.cpp
                science_ui/rml_science_chart.cpp
                science_ui/science_chart_model.cpp
                science_ui/science_report_window.cpp
                "${OSGSOL_RMLUI_GL3_PATCH_DIR}/RmlUi_Renderer_GL3.cpp")
            TARGET_COMPILE_FEATURES(osgSol_Test_RmlGlRuntimeSmoke PRIVATE
                cxx_std_17)
            TARGET_COMPILE_DEFINITIONS(osgSol_Test_RmlGlRuntimeSmoke PRIVATE
                OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                OSGSOL_UI_EVIDENCE_DIR="${CMAKE_BINARY_DIR}/ui-evidence")
            TARGET_INCLUDE_DIRECTORIES(osgSol_Test_RmlGlRuntimeSmoke PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}"
                "${OSGSOL_RMLUI_GL3_PATCH_DIR}"
                "${OSGSOL_RMLUI_SOURCE_DIR}/Backends")
            TARGET_LINK_LIBRARIES(osgSol_Test_RmlGlRuntimeSmoke
                osgVerseUI osgVerseReaderWriter osgVersePipeline
                RmlUi::RmlUi ${CMAKE_DL_LIBS}
                "-framework OpenGL")
            LINK_OSG_LIBRARY(osgSol_Test_RmlGlRuntimeSmoke
                OpenThreads osg osgGA osgViewer)
            ADD_TEST(NAME osgSol_Test_RmlGlRuntimeSmoke
                     COMMAND $<TARGET_FILE:osgSol_Test_RmlGlRuntimeSmoke>)
            SET_TESTS_PROPERTIES(osgSol_Test_RmlGlRuntimeSmoke PROPERTIES
                LABELS "offline;scienceearth;ui;gl"
                TIMEOUT 30)
        ENDIF()

        ADD_EXECUTABLE(osgSol_Test_ScienceChartModel
            "${CMAKE_SOURCE_DIR}/tests/science_chart_model_tests.cpp"
            science_ui/science_chart_model.cpp)
        TARGET_COMPILE_FEATURES(osgSol_Test_ScienceChartModel PRIVATE
            cxx_std_17)
        TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceChartModel PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}")
        ADD_TEST(NAME osgSol_Test_ScienceChartModel
                 COMMAND $<TARGET_FILE:osgSol_Test_ScienceChartModel>)
        SET_TESTS_PROPERTIES(osgSol_Test_ScienceChartModel PROPERTIES
            LABELS "offline;scienceearth;ui"
            TIMEOUT 30)

        ADD_EXECUTABLE(osgSol_Test_MapContextCapture
            "${CMAKE_SOURCE_DIR}/tests/map_context_capture_tests.cpp"
            science_ui/map_context_capture.cpp
            science_ui/science_target_overlay.cpp)
        TARGET_COMPILE_FEATURES(osgSol_Test_MapContextCapture PRIVATE
            cxx_std_17)
        TARGET_INCLUDE_DIRECTORIES(osgSol_Test_MapContextCapture PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}")
        LINK_OSG_LIBRARY(osgSol_Test_MapContextCapture OpenThreads osg osgDB)
        IF(APPLE)
            TARGET_LINK_LIBRARIES(osgSol_Test_MapContextCapture
                                  "-framework OpenGL")
        ENDIF()
        ADD_TEST(NAME osgSol_Test_MapContextCapture
                 COMMAND $<TARGET_FILE:osgSol_Test_MapContextCapture>)
        SET_TESTS_PROPERTIES(osgSol_Test_MapContextCapture PROPERTIES
            LABELS "offline;scienceearth;ui"
            TIMEOUT 30)

        ADD_EXECUTABLE(osgSol_Test_ScienceReportWindow
            "${CMAKE_SOURCE_DIR}/tests/science_report_window_tests.cpp"
            science_ui/science_report_window.cpp)
        TARGET_COMPILE_FEATURES(osgSol_Test_ScienceReportWindow PRIVATE
            cxx_std_17)
        TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceReportWindow PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}")
        LINK_OSG_LIBRARY(osgSol_Test_ScienceReportWindow OpenThreads osg)
        ADD_TEST(NAME osgSol_Test_ScienceReportWindow
                 COMMAND $<TARGET_FILE:osgSol_Test_ScienceReportWindow>)
        SET_TESTS_PROPERTIES(osgSol_Test_ScienceReportWindow PROPERTIES
            LABELS "offline;scienceearth;ui"
            TIMEOUT 30)

        ADD_EXECUTABLE(osgSol_Test_RmlInputBridge
            "${CMAKE_SOURCE_DIR}/tests/rml_input_bridge_tests.cpp"
            product_ui/rml_input_bridge.cpp)
        TARGET_COMPILE_FEATURES(osgSol_Test_RmlInputBridge PRIVATE cxx_std_17)
        TARGET_INCLUDE_DIRECTORIES(osgSol_Test_RmlInputBridge PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}")
        ADD_TEST(NAME osgSol_Test_RmlInputBridge
                 COMMAND $<TARGET_FILE:osgSol_Test_RmlInputBridge>)
        SET_TESTS_PROPERTIES(osgSol_Test_RmlInputBridge PROPERTIES
            LABELS "offline;scienceearth;ui"
            TIMEOUT 30)
    ENDIF()

    ADD_EXECUTABLE(osgSol_Test_ScienceTargetOverlay
        "${CMAKE_SOURCE_DIR}/tests/science_target_overlay_tests.cpp"
        science_ui/science_target_overlay.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceTargetOverlay PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceTargetOverlay PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    LINK_OSG_LIBRARY(osgSol_Test_ScienceTargetOverlay OpenThreads osg)
    ADD_TEST(NAME osgSol_Test_ScienceTargetOverlay
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceTargetOverlay>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceTargetOverlay PROPERTIES
        LABELS "offline;scienceearth;ui"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgSol_Test_ScienceWorkbenchProtocol
        "${CMAKE_SOURCE_DIR}/tests/science_workbench_protocol_tests.cpp"
        science_workbench_protocol.cpp
        science_workbench_model.cpp
        science_temporal_adapter.cpp
        project/earth_temporal_controller.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceWorkbenchProtocol PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceWorkbenchProtocol PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/science"
        "${CMAKE_SOURCE_DIR}/3rdparty")
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceWorkbenchProtocol
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceWorkbenchProtocol
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceWorkbenchProtocol>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceWorkbenchProtocol PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgSol_Test_ScienceWorkbenchModel
        "${CMAKE_SOURCE_DIR}/tests/science_workbench_model_tests.cpp"
        science_workbench_model.cpp
        science_temporal_adapter.cpp
        project/earth_temporal_controller.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceWorkbenchModel PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceWorkbenchModel PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/science")
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceWorkbenchModel
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceWorkbenchModel
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceWorkbenchModel>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceWorkbenchModel PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgSol_Test_ScienceTemporalAdapter
        "${CMAKE_SOURCE_DIR}/tests/science_temporal_adapter_tests.cpp")
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceTemporalAdapter PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceTemporalAdapter PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/science")
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceTemporalAdapter
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceTemporalAdapter
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceTemporalAdapter>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceTemporalAdapter PROPERTIES
        LABELS "offline;scienceearth;project"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgSol_Test_ScienceWorkbenchQueryPlan
        "${CMAKE_SOURCE_DIR}/tests/science_workbench_query_plan_tests.cpp")
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceWorkbenchQueryPlan PRIVATE
        cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceWorkbenchQueryPlan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/science")
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceWorkbenchQueryPlan
        PRIVATE osgSolScienceCore)
    ADD_TEST(NAME osgSol_Test_ScienceWorkbenchQueryPlan
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceWorkbenchQueryPlan>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceWorkbenchQueryPlan PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgSol_Test_TerrainScienceOverlayMath
        terrain_science_overlay_math_test.cpp
        terrain_science_overlay_math.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_TerrainScienceOverlayMath PRIVATE cxx_std_14)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_TerrainScienceOverlayMath PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    ADD_TEST(NAME osgSol_Test_TerrainScienceOverlayMath
             COMMAND $<TARGET_FILE:osgSol_Test_TerrainScienceOverlayMath>)
    SET_TESTS_PROPERTIES(osgSol_Test_TerrainScienceOverlayMath PROPERTIES
        LABELS "offline;scienceearth;terrain"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgSol_Test_TerrainScienceOverlay
        terrain_science_overlay_test.cpp
        terrain_science_overlay.cpp
        terrain_science_overlay_math.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_TerrainScienceOverlay PRIVATE cxx_std_14)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_TerrainScienceOverlay PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    LINK_OSG_LIBRARY(osgSol_Test_TerrainScienceOverlay OpenThreads osg osgDB)
    ADD_TEST(NAME osgSol_Test_TerrainScienceOverlay
             COMMAND $<TARGET_FILE:osgSol_Test_TerrainScienceOverlay>)
    SET_TESTS_PROPERTIES(osgSol_Test_TerrainScienceOverlay PROPERTIES
        LABELS "offline;scienceearth;terrain"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgSol_Test_ScienceEarthPanel
        science_earth_panel_test.cpp
        science_earth_panel.cpp
        science_preview_layer.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceEarthPanel PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceEarthPanel PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceEarthPanel
        osgSolSciencePreview osgVerseReaderWriter osgVersePipeline
        osgVerseModeling osgVerseDependency)
    LINK_OSG_LIBRARY(osgSol_Test_ScienceEarthPanel
        OpenThreads osg osgDB osgGA osgUtil osgViewer)
    ADD_TEST(NAME osgSol_Test_ScienceEarthPanel
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceEarthPanel>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceEarthPanel PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 30)

    ADD_EXECUTABLE(osgSol_Test_ScienceAiTools
        science_ai_tools_test.cpp
        science_ai_tools.cpp
        science_preview_layer.cpp)
    TARGET_COMPILE_FEATURES(osgSol_Test_ScienceAiTools PRIVATE cxx_std_17)
    TARGET_INCLUDE_DIRECTORIES(osgSol_Test_ScienceAiTools PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${OSGSOL_SCIENCE_DEPS_ROOT}/include")
    TARGET_LINK_LIBRARIES(osgSol_Test_ScienceAiTools
        osgSolSciencePreview osgVerseReaderWriter osgVersePipeline
        osgVerseModeling osgVerseDependency)
    LINK_OSG_LIBRARY(osgSol_Test_ScienceAiTools
        OpenThreads osg osgDB osgGA osgUtil osgViewer)
    ADD_TEST(NAME osgSol_Test_ScienceAiTools
             COMMAND $<TARGET_FILE:osgSol_Test_ScienceAiTools>)
    SET_TESTS_PROPERTIES(osgSol_Test_ScienceAiTools PROPERTIES
        LABELS "offline;scienceearth"
        TIMEOUT 30)
ENDIF()

USE_MSVC_DEBUGGER(${EXE_NAME} "${THIRDPARTY_ROOT}/bin;${OSG_BUILD_INCLUDE_DIR}/../bin")
IF(MSVC AND VERSE_INSTALL_PDB_FILES)
    INSTALL(FILES $<TARGET_PDB_FILE:${EXE_NAME}> DESTINATION ${INSTALL_BINDIR} OPTIONAL)
ENDIF()
