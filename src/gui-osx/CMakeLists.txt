
set(GUIOSX_SRCS AlertHooks.m PrefsController.m Shared.m
        CreateFloppyController.m SDLMain.m)

set_source_files_properties(${GUIOSX_SRCS} PROPERTIES LANGUAGE C)

add_library(GuiOsx ${GUIOSX_SRCS})

