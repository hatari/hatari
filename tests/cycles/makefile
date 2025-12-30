NAME = cyccheck

all: $(NAME).prg

$(NAME).prg: $(NAME).s
	sed -e 's/dc\.b."\(.*\)",\(.*\)/.ascii "\1"\r\n .byte \2/' \
	    -e 's/;.*//' -e 's/\$$/0x/g' -e 's/opt.x+//' \
	     $< > $<.tmp.S
	m68k-atari-mint-gcc -nostdlib -ffreestanding $<.tmp.S -o $@
	$(RM) $<.tmp.S
