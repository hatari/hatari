
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src/includes
		    ${CMAKE_SOURCE_DIR}/src/debug)

set(HMSA_SOURCES hmsa.c floppy.c ../../src/file.c)

if(ZLIB_FOUND)
	include_directories(${ZLIB_INCLUDE_DIR})
	set(HMSA_SOURCES ${HMSA_SOURCES}  ../../src/unzip.c)
endif(ZLIB_FOUND)

add_executable(hmsa ${HMSA_SOURCES})

target_link_libraries(hmsa Floppy)

if(Math_FOUND)
	target_link_libraries(hmsa ${MATH_LIBRARY})
endif(Math_FOUND)

if(ZLIB_FOUND)
	target_link_libraries(hmsa ${ZLIB_LIBRARY})
endif(ZLIB_FOUND)

install(TARGETS hmsa RUNTIME DESTINATION ${BINDIR})

if(ENABLE_MAN_PAGES)
	add_custom_target(hmsa_manpage ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hmsa.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hmsa.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hmsa.1 > ${CMAKE_CURRENT_BINARY_DIR}/hmsa.1.gz
		DEPENDS hmsa.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hmsa.1.gz DESTINATION ${MANDIR})
endif(ENABLE_MAN_PAGES)
