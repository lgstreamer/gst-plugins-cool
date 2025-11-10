### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-cool-@GST_API_VERSION@.pc \
	gstreamer-dvrmpegts-@GST_API_VERSION@.pc

pcverfiles_uninstalled = \
	gstreamer-cool-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-dvrmpegts-@GST_API_VERSION@-uninstalled.pc

all-local: $(pcverfiles) $(pcverfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate versioned .pc files from .pc files in this dir
%-@GST_API_VERSION@.pc: %.pc
	$(cp_verbose_0)cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
	$(cp_verbose_0)cp $< $@
### the uninstalled libdir is depend of the build system used so set it here
### rather than hardcoding it in the file directly.
	$(AM_V_GEN) sed \
		-e "s|[@]dvrmpegtslibdir[@]|$(abs_top_builddir)/gst-libs/gst/dvrmpegts/.libs|" \
		$< > $@.tmp && mv $@.tmp $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
		   gstreamer-cool.pc.in gstreamer-cool-uninstalled.pc.in \
		   gstreamer-dvrmpegts.pc.in gstreamer-dvrmpegts-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
