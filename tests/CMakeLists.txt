add_executable(scnc_tests
  test_gcode.cpp
  test_heightmap.cpp
  test_driver.cpp
  test_cam.cpp
)
target_link_libraries(scnc_tests PRIVATE scnc_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(scnc_tests)
