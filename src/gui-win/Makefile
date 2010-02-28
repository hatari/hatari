
include ../../Makefile.cnf

CFLAGS += -I../includes $(SDL_CFLAGS)

GUIWINOBJS = opencon.o

all: gui-win.a hatari-winicon.o

gui-win.a: $(GUIWINOBJS)
	$(AR) cru $@ $^
	$(RANLIB) $@

hatari-winicon.o:
	$(WINDRES) -o hatari-winicon.o hatari-winicon.rc

clean distclean:
	$(RM) *.o
