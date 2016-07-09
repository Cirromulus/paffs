MESSAGE(STATUS "paffs")
INCLUDE_DIRECTORIES(
${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/paffs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/paffs_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/btree.c
)

COMPILE_AS_CXX11()

add_subdirectory(driver)

add_library(paffs ${SOURCES})

#add_executable(btree_test ${SOURCES} tests/tree.c)
add_executable(dir_test tests/dirtest.c)

target_link_libraries(dir_test paffs simu)

set_target_properties(dir_test PROPERTIES
	COMPILE_FLAGS "-Wall -std=gnu99"
)

set_target_properties(paffs PROPERTIES
	COMPILE_FLAGS "-Wall -std=gnu99"
)
