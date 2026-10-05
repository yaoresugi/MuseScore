# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies

set(MODULE_TEST notationscene_widget_tests)
set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/environment.cpp
    ${CMAKE_CURRENT_LIST_DIR}/pitchpreviewdialog_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/previewmocks.h
    ${PROJECT_SOURCE_DIR}/src/engraving/tests/utils/scorerw.cpp
)
set(MODULE_TEST_LINK notationscene engraving muse::uicomponents Qt::Widgets)
# Unity builds can pull the action controller alongside the native dialog;
# its element-popup query is implemented in the notation scene QML target.
if(TARGET notationscene_qml)
    list(APPEND MODULE_TEST_LINK notationscene_qml)
endif()
# Share the engraving fixtures so native-widget and score tests exercise the
# same four-part score and the same real grace-note structure.
set(MODULE_TEST_DATA_ROOT ${PROJECT_SOURCE_DIR}/src/engraving/tests)
include(SetupGTest)

# The common runner constructs QGuiApplication. QWidget requires QApplication.
get_target_property(widget_test_sources ${MODULE_TEST} SOURCES)
list(FILTER widget_test_sources EXCLUDE REGEX "/gmain\\.cpp$")
set_property(TARGET ${MODULE_TEST} PROPERTY SOURCES ${widget_test_sources})
target_sources(${MODULE_TEST} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/widgetmain.cpp)
set_source_files_properties(pitchpreviewdialog_tests.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_tests_properties(${MODULE_TEST} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
