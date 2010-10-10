
install(PROGRAMS atari-hd-image.sh zip2st.sh  DESTINATION ${BINDIR})

add_subdirectory(hmsa)

include(FindPythonInterp)
if(PYTHONINTERP_FOUND)
	add_subdirectory(hconsole)
endif(PYTHONINTERP_FOUND)
