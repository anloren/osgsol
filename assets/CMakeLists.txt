MACRO(COPY_DIRECTORY TARGET_NAME IN_DIR OUT_DIR)
    ADD_CUSTOM_TARGET(${TARGET_NAME} ALL COMMAND ${CMAKE_COMMAND}
                      -E copy_directory "${IN_DIR}" "${OUT_DIR}")
    SET_PROPERTY(TARGET ${TARGET_NAME} PROPERTY FOLDER "RESOURCES")
ENDMACRO(COPY_DIRECTORY)

COPY_DIRECTORY(MiscFiles "${CMAKE_CURRENT_SOURCE_DIR}/misc" "${CMAKE_INSTALL_PREFIX}/misc")
COPY_DIRECTORY(ModelFiles "${CMAKE_CURRENT_SOURCE_DIR}/models" "${CMAKE_INSTALL_PREFIX}/models")
COPY_DIRECTORY(ShaderFiles "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${CMAKE_INSTALL_PREFIX}/shaders")
COPY_DIRECTORY(SkyboxFiles "${CMAKE_CURRENT_SOURCE_DIR}/skyboxes" "${CMAKE_INSTALL_PREFIX}/skyboxes")
COPY_DIRECTORY(TextureFiles "${CMAKE_CURRENT_SOURCE_DIR}/textures" "${CMAKE_INSTALL_PREFIX}/textures")

# The EarthExplorer executable resolves product UI documents from MISC_DIR at runtime.  Keep the
# resource copy in the dependency graph of targeted EarthExplorer builds; otherwise building only
# osgVerse_EarthExplorer can produce an executable whose installed tree silently lacks the RmlUi
# ScienceEarth workbench.
IF(TARGET osgVerse_EarthExplorer)
    ADD_DEPENDENCIES(osgVerse_EarthExplorer MiscFiles)
ENDIF()
