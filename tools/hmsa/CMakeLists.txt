
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src/includes
		    ${CMAKE_SOURCE_DIR}/src/debug ${SDL_INCLUDE_DIR})

if(ZLIB_FOUND)
	include_directories(${ZLIB_INCLUDE_DIR})
endif(ZLIB_FOUND)

add_executable(hmsa hmsa.c floppy.c ../../src/file.c ../../src/unzip.c)

target_link_libraries(hmsa Floppy)

if(MATH_FOUND)
	target_link_libraries(hmsa ${MATH_LIBRARY})
endif(MATH_FOUND)

if(ZLIB_FOUND)
	target_link_libraries(hmsa ${ZLIB_LIBRARY})
endif(ZLIB_FOUND)

install(TARGETS hmsa RUNTIME DESTINATION ${BINDIR})

# if(UNIX)
	add_custom_target(hmsa_manpage ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hmsa.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hmsa.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hmsa.1 > ${CMAKE_CURRENT_BINARY_DIR}/hmsa.1.gz
		DEPENDS hmsa.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hmsa.1.gz DESTINATION ${MANDIR})
# endif(UNIX)
