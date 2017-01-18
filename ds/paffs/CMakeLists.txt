MESSAGE(STATUS "paffs")
INCLUDE_DIRECTORIES(
${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/paffs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/paffs_flash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/btree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/treeCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/superblock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/garbage_collection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/treequeue.cpp #currently only used for debugging
)

COMPILE_AS_CXX11()

add_subdirectory(driver)

add_library(paffs ${SOURCES})
target_compile_definitions(paffs 
	PUBLIC TREENODECACHESIZE=5
)
set_target_properties(paffs PROPERTIES
	COMPILE_FLAGS "-Wall"
)


add_executable(dir_test tests/dirtest.c)
target_link_libraries(dir_test paffs simu)
set_target_properties(dir_test PROPERTIES
	COMPILE_FLAGS "-Wall"
)

add_executable(tree_test ${SOURCES} tests/tree.c)
target_link_libraries(tree_test paffs simu)
set_target_properties(tree_test PROPERTIES
	COMPILE_FLAGS "-Wall"
)

add_executable(sindex_test ${SOURCES} tests/superblocktest.c)
target_link_libraries(sindex_test paffs simu)
set_target_properties(sindex_test PROPERTIES
	COMPILE_FLAGS "-Wall"
)

