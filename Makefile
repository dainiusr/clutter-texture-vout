
libdir = $(shell pkg-config --variable=libdir vlc-plugin )
vlclibdir = $(libdir)/vlc/plugins/video_output

all: libclutter_texture_plugin.so

libclutter_texture_plugin.so: libclutter_texture_plugin.o
	gcc -shared  -std=gnu99 $< `pkg-config  --libs vlc-plugin clutter-1.0`  -Wl,-soname -Wl,$@ -o $@

libclutter_texture_plugin.o: clutter-texture-vout.c
	gcc -c -fPIC -std=gnu99  $< `pkg-config  --cflags vlc-plugin clutter-1.0` -D__PLUGIN__  -DMODULE_STRING=\"clutter\" -o $@

clean:
	rm -f libclutter_texture_plugin.o libclutter_texture_plugin.so

install: all
	mkdir -p $(DESTDIR)$(vlclibdir)/
	install -m 0755 libclutter_texture_plugin.so $(DESTDIR)$(vlclibdir)/

install-strip: all
	mkdir -p $(DESTDIR)$(vlclibdir)/
	install -s -m 0755 libclutter_texture_plugin.so $(DESTDIR)$(vlclibdir)/

uninstall:
	rm -f -- $(DESTDIR)$(vlclibdir)/libclutter_texture_plugin.so

.PHONY: all clean install uninstall
