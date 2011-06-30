#
# "distclean" target for removing the generated files from CMake
#

if(UNIX)
	add_custom_target(distclean  COMMENT "Cleaning up for distribution")
	# Clean up Hatari specific files:
	foreach(CLEAN_FILE  config.h install_manifest.txt
			src/hatari src/uae-cpu/build68k
			src/uae-cpu/cpudefs.c src/uae-cpu/cpuemu.c
			src/uae-cpu/cpustbl.c src/uae-cpu/cputbl.h
			src/uae-cpu/gencpu tools/hmsa/hmsa)
		add_custom_command(TARGET distclean POST_BUILD
			COMMAND rm -f ${CLEAN_FILE}
			DEPENDS clean)
	endforeach(CLEAN_FILE)
	# Clean up files that can appear at multiple places:
	foreach(CLEAN_FILE  CMakeFiles CMakeCache.txt '*.a' '*.1.gz'
			cmake_install.cmake Makefile)
		add_custom_command(TARGET distclean POST_BUILD
			COMMAND find . -depth -name ${CLEAN_FILE} | xargs rm -rf
			DEPENDS clean)
	endforeach(CLEAN_FILE)
endif(UNIX)
