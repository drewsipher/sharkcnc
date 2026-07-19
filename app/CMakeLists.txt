set(CMAKE_AUTOMOC ON)

qt_add_executable(sharkcnc
  main.cpp
  machine_client.cpp
  gcode_view.cpp
  main_window.cpp
  probe_dialog.cpp
  cam_panel.cpp
  tool_library.cpp
  tool_dialog.cpp
)
target_link_libraries(sharkcnc PRIVATE scnc_core Qt6::Widgets)
