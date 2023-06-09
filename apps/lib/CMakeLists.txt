# Kate shared library, used by Kate/KWrite and the addons
ecm_set_disabled_deprecation_versions(
    QT 5.15
    KF 5.90
)

add_library(kateprivate SHARED "")
set_target_properties(kateprivate PROPERTIES VERSION ${RELEASE_SERVICE_VERSION})
install(TARGETS kateprivate ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)

# ensure right translations domain, we use one kate.po(t) for lib + apps
target_compile_definitions(kateprivate PRIVATE TRANSLATION_DOMAIN="kate")

# export version to be used by Kate/KWrite/...
target_compile_definitions(kateprivate PUBLIC KATE_VERSION="${RELEASE_SERVICE_VERSION}")

include(GenerateExportHeader)
generate_export_header(
  kateprivate
  EXPORT_FILE_NAME kateprivate_export.h
  EXPORT_MACRO_NAME KATE_PRIVATE_EXPORT
)

target_include_directories(
  kateprivate
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/session
    ${CMAKE_CURRENT_SOURCE_DIR}/quickopen
    ${CMAKE_CURRENT_SOURCE_DIR}/diff
    ${CMAKE_CURRENT_BINARY_DIR} # kateprivate_export.h
)

find_package(
  KF${KF_MAJOR_VERSION} ${KF5_DEP_VERSION}
  QUIET
  REQUIRED
  COMPONENTS
    Crash
    IconThemes
    TextEditor
    TextWidgets
    WindowSystem
)

if (NOT WIN32 AND NOT APPLE)
    find_package(KF${KF_MAJOR_VERSION} ${KF5_DEP_VERSION} OPTIONAL_COMPONENTS Activities)
endif()

target_link_libraries(
  kateprivate
  PUBLIC
    KF${KF_MAJOR_VERSION}::CoreAddons
    KF${KF_MAJOR_VERSION}::Crash
    KF${KF_MAJOR_VERSION}::I18n
    KF${KF_MAJOR_VERSION}::IconThemes
    KF${KF_MAJOR_VERSION}::TextEditor
    KF${KF_MAJOR_VERSION}::TextWidgets
    KF${KF_MAJOR_VERSION}::WindowSystem
    KF${KF_MAJOR_VERSION}::KIOGui
    KF${KF_MAJOR_VERSION}::KIOWidgets
)

if (TARGET Qt6::Core5Compat)
    target_link_libraries(kateprivate PRIVATE Qt6::Core5Compat) # QTextCodec
endif()

if(KF5Activities_FOUND)
  target_link_libraries(kateprivate PUBLIC KF5::Activities)
  target_compile_definitions(kateprivate PUBLIC -DKF5Activities_FOUND)
endif()

if (NOT WIN32 AND NOT HAIKU)
    target_compile_definitions(kateprivate PRIVATE -DKDE_INSTALL_FULL_LOCALEDIR=\"${KDE_INSTALL_FULL_LOCALEDIR}\")
    target_include_directories(kateprivate PRIVATE ${Intl_INCLUDE_DIRS})
    target_link_libraries(kateprivate PRIVATE ${Intl_LIBRARIES})
endif()

# optional KUserFeedback integration
set(KUSERFEEDBACK_SUFFIX)
if (QT_MAJOR_VERSION STREQUAL "6")
    set(KUSERFEEDBACK_SUFFIX "Qt6")
    find_package(KUserFeedback${KUSERFEEDBACK_SUFFIX} 1.2.1 CONFIG)
    target_compile_definitions(kateprivate PUBLIC -DUSE_KUSERFEEDBACK_QT6)
else()
    find_package(KUserFeedback${KUSERFEEDBACK_SUFFIX} 1.2.0 CONFIG)
endif()

if (TARGET KUserFeedbackWidgets${KUSERFEEDBACK_SUFFIX})
    target_link_libraries(kateprivate PUBLIC KUserFeedbackWidgets${KUSERFEEDBACK_SUFFIX})
    target_compile_definitions(kateprivate PUBLIC -DWITH_KUSERFEEDBACK)
endif()

# terminal detection
include(CheckFunctionExists)
check_function_exists(ctermid HAVE_CTERMID)
if(HAVE_CTERMID)
  target_compile_definitions(kateprivate PRIVATE HAVE_CTERMID)
endif()

# detaching from terminal
check_function_exists(daemon HAVE_DAEMON)
if(HAVE_DAEMON)
  target_compile_definitions(kateprivate PRIVATE HAVE_DAEMON)
endif()

ki18n_wrap_ui(kateprivate ui/sessionconfigwidget.ui welcomeview/welcomeview.ui session/katesessionmanagedialog.ui)

target_sources(
  kateprivate
  PRIVATE
    session/katesession.cpp
    session/katesessionmanagedialog.cpp
    session/katesessionmanager.cpp
    session/katesessionsaction.cpp

    quickopen/katequickopen.cpp
    quickopen/katequickopenmodel.cpp
    quickopen/katequickopenlineedit.cpp

    kateapp.cpp
    kateappadaptor.cpp
    kateconfigdialog.cpp
    kateconfigplugindialogpage.cpp
    katedocmanager.cpp
    katefileactions.cpp
    katemainwindow.cpp
    katemdi.cpp
    katemwmodonhddialog.cpp
    katepluginmanager.cpp

    katerunninginstanceinfo.cpp
    katesavemodifieddialog.cpp
    katetabbar.cpp
    katesplitter.cpp
    kateviewmanager.cpp
    kateviewspace.cpp
    tabmimedata.cpp

    kateoutputview.cpp
    katestashmanager.cpp
    diagnostics/diagnosticview.cpp
    texthint/KateTextHintManager.cpp
    texthint/tooltip.cpp

    kateurlbar.cpp

    signal_watcher.cpp
    gitprocess.cpp
    quickdialog.cpp
    ktexteditor_utils.cpp

    data/kateprivate.qrc
    hostprocess.cpp

    diff/diffwidget.cpp
    diff/diffeditor.cpp
    diff/difflinenumarea.cpp
    diff/gitdiff.cpp

    filehistorywidget.cpp
    commitfilesview.cpp

    welcomeview/welcomeview.cpp
    welcomeview/recentitemsmodel.cpp
    welcomeview/savedsessionsmodel.cpp
)

if (BUILD_PCH)
    target_precompile_headers(kateprivate REUSE_FROM katepch)
endif()

if(BUILD_TESTING)
  add_subdirectory(autotests)
endif()
