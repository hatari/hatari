
set(TEST_SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests/debugger)

include(FindPython)

include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src/includes
                    ${CMAKE_SOURCE_DIR}/src/debug)

if(ZLIB_FOUND)
	 include_directories(${ZLIB_INCLUDE_DIR})
endif(ZLIB_FOUND)


add_executable(test-file test-file.c ${CMAKE_SOURCE_DIR}/src/file.c)
if(ZLIB_FOUND)
	target_link_libraries(test-file ${ZLIB_LIBRARY})
endif(ZLIB_FOUND)
add_test(NAME unit-file COMMAND test-file)

add_executable(test-buserror test-buserror.c ${CMAKE_SOURCE_DIR}/src/buserror.c)
add_test(NAME unit-buserror COMMAND test-buserror)

if(Python_Interpreter_FOUND)
	add_test(NAME unit-python-tools
	         COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test-python-tools.py)
endif()
