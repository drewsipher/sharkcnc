add_library(scnc_core STATIC
  gcode/parser.cpp
  gcode/warp.cpp
  heightmap/heightmap.cpp
  machine/grbl_driver.cpp
  machine/simulator.cpp
  transport/tcp_transport.cpp
  transport/serial_transport.cpp
  cam/gerber.cpp
  cam/excellon.cpp
  cam/isolation.cpp
  cam/facing.cpp
  cam/outline.cpp
  cam/gcode_out.cpp
  cam/tool.cpp
)
target_include_directories(scnc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scnc_core PUBLIC Clipper2)
find_package(Threads REQUIRED)
target_link_libraries(scnc_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(scnc_core PRIVATE /W4)
else()
  target_compile_options(scnc_core PRIVATE -Wall -Wextra)
endif()
