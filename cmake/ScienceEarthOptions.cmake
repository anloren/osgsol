OPTION(OSGSOL_BUILD_SCIENCE "Build optional ScienceEarth providers" OFF)
SET(OSGSOL_SCIENCE_DEPS_ROOT "" CACHE PATH "Trimmed ScienceEarth dependency prefix")
OPTION(OSGSOL_SCIENCE_NETWORK_TESTS "Enable live scientific-source tests" OFF)
OPTION(OSGSOL_BUILD_RMLUI_PRODUCT_UI
       "Build the disabled-by-default RmlUi product interface" OFF)
SET(OSGSOL_PRODUCT_UI "legacy" CACHE STRING
    "Product UI selector: legacy or rml")
SET_PROPERTY(CACHE OSGSOL_PRODUCT_UI PROPERTY STRINGS legacy rml)
