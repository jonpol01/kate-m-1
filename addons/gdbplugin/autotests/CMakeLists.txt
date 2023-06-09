include(ECMMarkAsTest)

add_executable(gdbplugin_test_gdbmi "")
target_include_directories(gdbplugin_test_gdbmi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Qt${QT_MAJOR_VERSION}Test ${QT_MIN_VERSION} QUIET REQUIRED)
target_link_libraries(
  gdbplugin_test_gdbmi
  PRIVATE
    KF${KF_MAJOR_VERSION}::I18n
    KF${KF_MAJOR_VERSION}::TextEditor
    Qt::Test
)

target_sources(
  gdbplugin_test_gdbmi
  PRIVATE
    tst_gdbmiitems.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../gdbmi/tokens.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../gdbmi/parser.cpp
)

add_test(NAME plugin-gdb_test_gdbmi COMMAND gdbplugin_test_gdbmi)
ecm_mark_as_test(gdbplugin_test_gdbmi)


add_executable(gdbplugin_test_gdbbackend "")
target_include_directories(gdbplugin_test_gdbbackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Qt${QT_MAJOR_VERSION}Test ${QT_MIN_VERSION} QUIET REQUIRED)
target_link_libraries(
  gdbplugin_test_gdbbackend
  PRIVATE
    kateprivate
    KF${KF_MAJOR_VERSION}::I18n
    KF${KF_MAJOR_VERSION}::TextEditor
    Qt::Test
)
ki18n_wrap_ui(gdbplugin_test_gdbbackend ${CMAKE_CURRENT_SOURCE_DIR}/../advanced_settings.ui)

target_sources(
  gdbplugin_test_gdbbackend
  PRIVATE
    tst_gdb_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../debugview.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../debugview_iface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../gdbvariableparser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../advanced_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dap/entities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../gdbmi/tokens.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../gdbmi/parser.cpp
)

add_test(NAME plugin-gdb_test_gdbbackend COMMAND gdbplugin_test_gdbbackend)
ecm_mark_as_test(gdbplugin_test_gdbbackend)
