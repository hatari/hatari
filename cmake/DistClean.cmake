#
# "distclean" target for removing the generated files from CMake
#

if(UNIX)
	add_custom_target(distclean  COMMENT "Cleaning up for distribution")
	foreach(CLEAN_FILE  CMakeFiles CMakeCache.txt cmake_install.cmake
			    Makefile config.h)
		add_custom_command(TARGET distclean POST_BUILD
			COMMAND find . -depth -name ${CLEAN_FILE} | xargs rm -r
			DEPENDS clean)
	endforeach(CLEAN_FILE)
endif(UNIX)
