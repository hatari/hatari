# Generic Makefile targets

all:
	@echo "use: make -f Makefile.[vbc|gcc|ahc]"

clean:
	$(RM) *.o *.tmp *~

PKG := natfeats-$(shell date -I).zip

package: $(NAME) clean
	cd ..; \
	$(RM) $(PKG); \
	zip -r $(PKG) $(NAME)/*
