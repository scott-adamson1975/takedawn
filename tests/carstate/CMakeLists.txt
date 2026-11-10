# carstate - the car snapshot's maths (src/game/carstate_math.h): speed, rates,
# accelerations, the filters' restarts. Pure: no guest, every host.
#   cmake --build <build> --target bo3_carstate_test && ctest -R bo3_carstate
# BO3_CARSTATE_REPLAY=<prefix of a BO3_TELEMETRY recording> also replays it.
add_executable(bo3_carstate_test test_main.c)
if(NOT WIN32)
    target_link_libraries(bo3_carstate_test PRIVATE m)
endif()
target_include_directories(bo3_carstate_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_carstate COMMAND bo3_carstate_test)
