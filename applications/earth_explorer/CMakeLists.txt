SET(EXE_NAME osgVerse_EarthExplorer)
SET(EXECUTABLE_FILES
    earth_main.cpp ui.cpp render_effects.cpp
    city_data.cpp ReaderWriterCSV.cpp precip_data.cpp flight_data.cpp tiles3d_data.cpp sat_math.cpp sat_data.cpp ais_math.cpp ais_data.cpp
    earth_config.cpp marker_style.cpp
    ai_chat.cpp ai_setup.cpp ai_ui.cpp ai_cards.cpp ai_media.cpp ai_query.cpp ai_world_tools.cpp
    feed_layer.cpp feeds/gdacs_feed.cpp feeds/usgs_quakes_feed.cpp feeds/eonet_feed.cpp
    feeds/gdelt_feed.cpp feeds/gpsjam_feed.cpp feeds/unhcr_feed.cpp feeds/nhc_feed.cpp
    feeds/strategic_feed.cpp feeds/firms_feed.cpp
    geo_primitives.cpp
)

IF(APPLE)
    # 中文 IME 直打桥(Objective-C++,给 OSG Cocoa 窗口挂 NSTextInputClient overlay,
    # 设计说明见 ime_bridge.h)。不能在子目录 ENABLE_LANGUAGE(OBJCXX)(CMake 语言
    # 信息不向兄弟目录传播,报 CMAKE_OBJCXX_COMPILE_OBJECT 缺失),改为按 CXX 规则
    # 编译 + "-x objective-c++" 强制 clang 走 ObjC++ 前端,零全局影响。
    SET_SOURCE_FILES_PROPERTIES(ime_bridge.mm PROPERTIES
                                LANGUAGE CXX COMPILE_FLAGS "-x objective-c++")
    SET(EXECUTABLE_FILES ${EXECUTABLE_FILES} ime_bridge.mm)
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
LINK_OSG_LIBRARY(${EXE_NAME} OpenThreads osg osgDB osgUtil osgGA osgText osgSim osgTerrain osgViewer)
TARGET_COMPILE_OPTIONS(${EXE_NAME} PUBLIC -D_SCL_SECURE_NO_WARNINGS)
IF(APPLE)
    TARGET_LINK_LIBRARIES(${EXE_NAME} "-framework Cocoa")   # ime_bridge.mm 的 AppKit 依赖
ENDIF(APPLE)

USE_MSVC_DEBUGGER(${EXE_NAME} "${THIRDPARTY_ROOT}/bin;${OSG_BUILD_INCLUDE_DIR}/../bin")
IF(MSVC AND VERSE_INSTALL_PDB_FILES)
    INSTALL(FILES $<TARGET_PDB_FILE:${EXE_NAME}> DESTINATION ${INSTALL_BINDIR} OPTIONAL)
ENDIF()
