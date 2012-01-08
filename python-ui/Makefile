# re-generate conftypes.py if configuration.c has changed
conftypes.py: ../src/configuration.c gentypes.py
	./gentypes.py < ../src/configuration.c > conftypes.py
