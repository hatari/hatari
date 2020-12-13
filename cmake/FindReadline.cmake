
IF (READLINE_INCLUDE_DIR)
	# Already in cache, be silent
	SET(READLINE_FIND_QUIETLY TRUE)
ENDIF (READLINE_INCLUDE_DIR)

FIND_PATH(READLINE_INCLUDE_DIR readline.h PATH_SUFFIXES readline)

FIND_LIBRARY(READLINE_LIBRARY NAMES readline)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Readline DEFAULT_MSG
                                  READLINE_LIBRARY READLINE_INCLUDE_DIR)

MARK_AS_ADVANCED(READLINE_LIBRARY READLINE_INCLUDE_DIR)

INCLUDE(CheckSymbolExists)

if(Readline_FOUND)
	set(CMAKE_REQUIRED_LIBRARIES ${READLINE_LIBRARY})
	set(CMAKE_REQUIRED_INCLUDES ${READLINE_INCLUDE_DIR})
	check_symbol_exists(rl_filename_completion_function
			    "stdio.h;readline.h"
			    HAVE_RL_COMPLETION_FUNCTION)
	# If linking did not work, we might have to link
	# explicitly against libtermcap or libncurses
	if(NOT HAVE_RL_COMPLETION_FUNCTION)
		unset(Readline_FOUND)
		find_package(Termcap)
		if(Termcap_FOUND)
			set(CMAKE_REQUIRED_LIBRARIES "readline" "termcap")
			check_symbol_exists(rl_filename_completion_function
					    "termcap.h"
					    HAVE_RL_COMPLETION_FUNCTION_TERMCAP)
		endif(Termcap_FOUND)
		if(HAVE_RL_COMPLETION_FUNCTION_TERMCAP)
			set(READLINE_LIBRARY ${READLINE_LIBRARY} ${TERMCAP_LIBRARY})
			set(Readline_FOUND TRUE)
		else(HAVE_RL_COMPLETION_FUNCTION_TERMCAP)
			find_package(Curses)
			if(Curses_FOUND)
				if(CURSES_NCURSES_LIBRARY)
					set(CMAKE_REQUIRED_LIBRARIES "readline" "ncurses")
              				check_symbol_exists(rl_filename_completion_function
							    "ncurses.h"
 				                    	    HAVE_RL_COMPLETION_FUNCTION_CURSES)
				else()
					set(CMAKE_REQUIRED_LIBRARIES "readline" "curses")
					check_symbol_exists(rl_filename_completion_function
							    "curses.h"
				                    	    HAVE_RL_COMPLETION_FUNCTION_CURSES)
				endif()
				if(HAVE_RL_COMPLETION_FUNCTION_CURSES)
					set(READLINE_LIBRARY
					    ${READLINE_LIBRARY} ${CURSES_LIBRARIES})
					set(Readline_FOUND TRUE)
				endif(HAVE_RL_COMPLETION_FUNCTION_CURSES)
			endif(Curses_FOUND)
		endif(HAVE_RL_COMPLETION_FUNCTION_TERMCAP)
	endif(NOT HAVE_RL_COMPLETION_FUNCTION)
	set(CMAKE_REQUIRED_LIBRARIES "")
	set(CMAKE_REQUIRED_INCLUDES "")
endif(Readline_FOUND)
