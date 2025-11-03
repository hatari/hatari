NAME = cyccheck

all: $(NAME).prg $(NAME).sym

$(NAME).prg: $(NAME).s
	# TODO: built how?

# NOTE: requires Hatari 'gst2ascii' version
# d0c691ee6de (2025-11-01) or later, as
# program has unnamed symbols
$(NAME).sym: $(NAME).prg
	gst2ascii -b -d $< > $@
