
include ../../Makefile.cnf

CFLAGS += -I../includes $(SDL_CFLAGS)

GUIWINOBJS = hatari-winicon.o opencon.o

all: gui-win.a

gui-win.a: $(GUIWINOBJS)
	$(AR) cru $@ $^
	$(RANLIB) $@

hatari-winicon.o:
	windres -o hatari-winicon.o hatari-winicon.rc

clean distclean:
	$(RM) *.o
