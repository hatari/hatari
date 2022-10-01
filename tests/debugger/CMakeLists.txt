
set(TEST_SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests/debugger)

include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src/includes
		    ${CMAKE_SOURCE_DIR}/src/debug ${CMAKE_SOURCE_DIR}/src/falcon
		    ${CMAKE_SOURCE_DIR}/src/cpu)

add_library(DebuggerTestLib test-dummies.c  ${CMAKE_SOURCE_DIR}/src/str.c
	    ${CMAKE_SOURCE_DIR}/src/debug/breakcond.c
	    ${CMAKE_SOURCE_DIR}/src/debug/debugcpu.c
	    ${CMAKE_SOURCE_DIR}/src/debug/history.c
	    ${CMAKE_SOURCE_DIR}/src/debug/evaluate.c
	    ${CMAKE_SOURCE_DIR}/src/debug/symbols.c
	    ${CMAKE_SOURCE_DIR}/src/debug/vars.c)

add_executable(test-breakcond test-breakcond.c)
target_link_libraries(test-breakcond DebuggerTestLib)
add_test(NAME debugger-breakcond WORKING_DIRECTORY ${TEST_SOURCE_DIR}
         COMMAND test-breakcond)

add_executable(test-evaluate test-evaluate.c)
target_link_libraries(test-evaluate DebuggerTestLib)
add_test(NAME debugger-evaluate WORKING_DIRECTORY ${TEST_SOURCE_DIR}
         COMMAND test-evaluate)

add_executable(test-symbols test-symbols.c)
target_link_libraries(test-symbols DebuggerTestLib)
add_test(NAME debugger-symbols WORKING_DIRECTORY ${TEST_SOURCE_DIR}
         COMMAND test-symbols)
