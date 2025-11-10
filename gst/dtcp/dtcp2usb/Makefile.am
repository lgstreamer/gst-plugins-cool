plugin_LTLIBRARIES = libgstdtcp2usb.la

# sources used to compile this plug-in
libgstdtcp2usb_la_SOURCES = gstdtcp2usb.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstdtcp2usb_la_CFLAGS = $(GST_CFLAGS) $(DTCP2USB_CFLAGS)
libgstdtcp2usb_la_LIBADD = $(GST_LIBS) $(DTCP2USB_LIBS) -lsvbdecryptor
libgstdtcp2usb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdtcp2usb_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstdtcp2usb.h
