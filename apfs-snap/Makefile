SRCS = apfs-snap.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

DESTDIR ?= ~
BINDIR = /bin
MANDIR = /share/man/man8

SPARSE_VERSION := $(shell sparse --version 2>/dev/null)

GIT_COMMIT = "r$(shell git rev-list --count HEAD).$(shell git rev-parse --short=7 HEAD)"

override CFLAGS += -Wall -Wno-address-of-packed-member -fno-strict-aliasing

apfs-snap: $(OBJS)
	@echo '  Linking...'
	@$(CC) $(CFLAGS) $(LDFLAGS) -o apfs-snap $(OBJS)
	@echo '  Build complete'

%.o: %.c
	@echo '  Compiling $<...'
	@$(CC) $(CFLAGS) -o $@ -MMD -MP -c $<
ifdef SPARSE_VERSION
	@sparse $(CFLAGS) $<
endif

apfs-snap.o: version.h
version.h: FORCE
	@printf '#define GIT_COMMIT\t"%s"\n' $(GIT_COMMIT) > version.h
FORCE:

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) apfs-snap version.h
install:
	install -d $(DESTDIR)$(BINDIR)
	install -t $(DESTDIR)$(BINDIR) apfs-snap
	install -d $(DESTDIR)$(MANDIR)
	install -m 644 -t $(DESTDIR)$(MANDIR) apfs-snap.8
