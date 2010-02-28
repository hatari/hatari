
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src/includes
		    ${CMAKE_SOURCE_DIR}/src/debug ${SDL_INCLUDE_DIR})

add_executable(hmsa hmsa.c floppy.c ../../src/file.c ../../src/unzip.c)

target_link_libraries(hmsa Floppy)

if(MATH_FOUND)
	target_link_libraries(hmsa ${MATH_LIBRARY})
endif(MATH_FOUND)

if(ZLIB_FOUND)
	target_link_libraries(hmsa ${ZLIB_LIBRARY})
endif(ZLIB_FOUND)

install(TARGETS hmsa RUNTIME DESTINATION ${BINDIR})
