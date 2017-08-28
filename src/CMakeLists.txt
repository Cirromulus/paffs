MESSAGE(STATUS "paffs")
INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/config/standard.hpp
)

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/paffs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/area.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dataIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/btree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/treeCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/superblock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/garbage_collection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/summaryCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/treequeue.cpp #currently only used for debugging
)

COMPILE_AS_CXX11()

add_subdirectory(driver)

add_library(paffs ${SOURCES})

set_target_properties(paffs PROPERTIES
	COMPILE_FLAGS "-Wall"
)

target_link_libraries(paffs simuDriver)

#add_subdirectory(tests)