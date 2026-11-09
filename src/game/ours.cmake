# The fork's sources, added to the game from src/game/CMakeLists.txt with one include (so that upstream's source lists
# stay as they are): the VR seat camera, the shared car snapshot, the wheel's feed, telemetry, and SimHub over UDP.
target_sources(burnout3 PRIVATE telemetry.c wheelfeed.c seatcam.c carstate.c simhub_udp.c)
if(WIN32)
    target_link_libraries(burnout3 PRIVATE ws2_32)    # Winsock for simhub_udp.c
endif()
