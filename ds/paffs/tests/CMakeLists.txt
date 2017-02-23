MESSAGE(STATUS "tests")

INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/googletest/gmock
	${CMAKE_CURRENT_SOURCE_DIR}/googletest/gtest
)

set(GTEST_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/googletest/gmock-gtest-all.cc
)

set(SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/unittests.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/treeCache.cpp
)

add_library(gtest ${GTEST_SOURCES})

add_executable(unittests ${SOURCES})
target_link_libraries(unittests pthread gtest)

add_executable(dir_test dirtest.cpp)
target_link_libraries(dir_test paffs simu)
set_target_properties(dir_test PROPERTIES
	COMPILE_FLAGS "-Wall"
)

#add_executable(tree_test tree.cpp)
#target_link_libraries(tree_test paffs simu)
#set_target_properties(tree_test PROPERTIES
#	COMPILE_FLAGS "-Wall"
#)

#add_executable(sindex_test superblocktest.cpp)
#target_link_libraries(sindex_test paffs simu)
#set_target_properties(sindex_test PROPERTIES
#	COMPILE_FLAGS "-Wall"
#)
