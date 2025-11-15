NAME = cyccheck

all: $(NAME).prg $(NAME).sym

$(NAME).prg: $(NAME).s
	sed -e 's/dc\.b."\(.*\)",\(.*\)/.ascii "\1"\r\n .byte \2/' \
	    -e 's/;.*//' -e 's/\$$/0x/g' -e 's/opt.x+//' \
	     $< > $<.tmp.S
	m68k-atari-mint-gcc -nostdlib -ffreestanding $<.tmp.S -o $@
	$(RM) $<.tmp.S

# NOTE: requires Hatari 'gst2ascii' version
# d0c691ee6de (2025-11-01) or later, as
# program has unnamed symbols
$(NAME).sym: $(NAME).prg
	gst2ascii -b -d $< > $@
