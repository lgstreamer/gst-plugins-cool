SUBDIRS = cool dvrmpegts

DIST_SUBDIRS = dvrmpegts

INDEPENDENT_SUBDIRS = \
	cool dvrmpegts

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs

