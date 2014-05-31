
IF (READLINE_INCLUDE_DIR)
	# Already in cache, be silent
	SET(READLINE_FIND_QUIETLY TRUE)
ENDIF (READLINE_INCLUDE_DIR)

FIND_PATH(READLINE_INCLUDE_DIR readline.h PATH_SUFFIXES readline)

FIND_LIBRARY(READLINE_LIBRARY NAMES readline)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(READLINE DEFAULT_MSG
                                  READLINE_LIBRARY READLINE_INCLUDE_DIR)

MARK_AS_ADVANCED(READLINE_LIBRARY READLINE_INCLUDE_DIR)

if(READLINE_FOUND)
	set(CMAKE_REQUIRED_LIBRARIES "readline")
	check_function_exists(rl_filename_completion_function
	                      HAVE_RL_COMPLETION_FUNCTION)
	# If linking did not work, we might have to link
	# explicitely against libtermcap or libncurses
	if(NOT HAVE_RL_COMPLETION_FUNCTION)
		unset(READLINE_FOUND)
		find_package(Termcap)
		if(TERMCAP_FOUND)
			set(CMAKE_REQUIRED_LIBRARIES "readline" "termcap")
			check_function_exists(rl_filename_completion_function
			                      HAVE_RL_COMPLETION_FUNCTION_TERMCAP)
		endif(TERMCAP_FOUND)
		if(HAVE_RL_COMPLETION_FUNCTION_TERMCAP)
			set(READLINE_LIBRARY ${READLINE_LIBRARY} ${TERMCAP_LIBRARY})
			set(READLINE_FOUND TRUE)
		else(HAVE_RL_COMPLETION_FUNCTION_TERMCAP)
			find_package(Curses)
			if(CURSES_FOUND)
				if(CURSES_NCURSES_LIBRARY)
					set(CMAKE_REQUIRED_LIBRARIES "readline" "ncurses")
				else()
					set(CMAKE_REQUIRED_LIBRARIES "readline" "curses")
				endif()
				check_function_exists(rl_filename_completion_function
				                      HAVE_RL_COMPLETION_FUNCTION_CURSES)
				if(HAVE_RL_COMPLETION_FUNCTION_CURSES)
					set(READLINE_LIBRARY
					    ${READLINE_LIBRARY} ${CURSES_LIBRARIES})
					set(READLINE_FOUND TRUE)
				endif(HAVE_RL_COMPLETION_FUNCTION_CURSES)
			endif(CURSES_FOUND)
		endif(HAVE_RL_COMPLETION_FUNCTION_TERMCAP)
	endif(NOT HAVE_RL_COMPLETION_FUNCTION)
	set(CMAKE_REQUIRED_LIBRARIES "")
endif(READLINE_FOUND)
