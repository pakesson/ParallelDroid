GTKFLAGS = $(shell pkg-config --libs --cflags gtk+-2.0 gthread-2.0)

all: gtk-ui

gtk-ui: gtk-ui.c
	gcc gtk-ui.c -o gtk-ui $(GTKFLAGS)

clean:
	rm -rf gtk-ui

.PHONY: clean
.SILENT: clean
