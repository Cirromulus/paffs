INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES ${SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/simuDriver.cpp
)

add_library(simuDriver ${SOURCES})

set_target_properties(simuDriver PROPERTIES
	COMPILE_FLAGS "-Wall"
)

target_link_libraries(simuDriver simu)
