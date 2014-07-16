# Generic Makefile targets

all:
	@echo "Use AHCC project file, or: make -f Makefile.[vbc|gcc]"

clean:
	$(RM) *.o *.tmp *~

PKG := natfeats-$(shell date -I).zip

package: $(NAME) clean
	cd ..; \
	$(RM) $(PKG); \
	zip -r $(PKG) $(NAME)/*
