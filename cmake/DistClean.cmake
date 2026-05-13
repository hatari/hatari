#
# "distclean" target for removing the generated files from CMake
#

if(UNIX)
	add_custom_target(distclean  COMMENT "Cleaning up for distribution")
	if (CMAKE_GENERATOR STREQUAL "Unix Makefiles")
		add_custom_command(TARGET distclean POST_BUILD
			COMMAND make clean)
	endif()
	# Clean up Hatari specific files:
	set(HATARI_DIST_CLEAN_FILES config.h install_manifest.txt
			src/*cpu/cpudefs.c src/*cpu/cpuemu*.c
			src/*cpu/cpustbl.c src/*cpu/cputbl.h)
	if(HATARI_ENABLE_PYTHON_TOOLS)
		list(APPEND HATARI_DIST_CLEAN_FILES python-ui/conftypes.py)
	endif(HATARI_ENABLE_PYTHON_TOOLS)
	foreach(CLEAN_FILE ${HATARI_DIST_CLEAN_FILES})
		add_custom_command(TARGET distclean POST_BUILD
			COMMAND rm -f ${CLEAN_FILE})
	endforeach(CLEAN_FILE)
	# Clean up files that can appear at multiple places:
	foreach(CLEAN_FILE  CMakeFiles CMakeCache.txt cmake_install.cmake
			CTestTestfile.cmake Makefile Testing
			'*.a' '*.1.gz' '*.pyc')
		add_custom_command(TARGET distclean POST_BUILD
			COMMAND find . -depth -name ${CLEAN_FILE} | xargs rm -rf)
	endforeach(CLEAN_FILE)
endif(UNIX)
