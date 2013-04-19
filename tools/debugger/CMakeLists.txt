
include_directories(${SDL_INCLUDE_DIR})

add_executable(gst2ascii gst2ascii.c)

install(TARGETS gst2ascii RUNTIME DESTINATION ${BINDIR})

install(PROGRAMS hatari_profile.py DESTINATION ${BINDIR})

# if(UNIX)
	add_custom_target(gst2ascii_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gst2ascii.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gst2ascii.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/gst2ascii.1 > ${CMAKE_CURRENT_BINARY_DIR}/gst2ascii.1.gz
		DEPENDS gst2ascii.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/gst2ascii.1.gz DESTINATION ${MANDIR})

	add_custom_target(hatari_profile_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hatari_profile.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hatari_profile.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hatari_profile.1 > ${CMAKE_CURRENT_BINARY_DIR}/hatari_profile.1.gz
		DEPENDS hatari_profile.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hatari_profile.1.gz DESTINATION ${MANDIR})
# endif(UNIX)
