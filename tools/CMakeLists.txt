add_executable(sharkcam sharkcam.cpp)
target_link_libraries(sharkcam PRIVATE scnc_core)

add_executable(scnc-send scnc_send.cpp)
target_link_libraries(scnc-send PRIVATE scnc_core)

if(NOT WIN32)
  add_executable(scnc-sim scnc_sim.cpp)
  target_link_libraries(scnc-sim PRIVATE scnc_core)
endif()
