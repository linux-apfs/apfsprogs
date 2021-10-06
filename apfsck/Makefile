SRCS = apfsck.c btree.c dir.c extents.c htable.c \
       inode.c key.c object.c spaceman.c super.c xattr.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

LIBDIR = ../lib
LIBRARY = $(LIBDIR)/libapfs.a

DESTDIR ?= ~
BINDIR = /bin
MANDIR = /share/man/man8

SPARSE_VERSION := $(shell sparse --version 2>/dev/null)

override CFLAGS += -Wall -Wno-address-of-packed-member -fno-strict-aliasing -I$(CURDIR)/../include

apfsck: $(OBJS) $(LIBRARY)
	@echo '  Linking...'
	@gcc $(CFLAGS) $(LDFLAGS) -o apfsck $(OBJS) $(LIBRARY)
	@echo '  Build complete'

# Build the common libraries
$(LIBRARY): FORCE
	@echo '  Building libraries...'
	@$(MAKE) -C $(LIBDIR) --silent --no-print-directory
	@echo '  Library build complete'
FORCE:

%.o: %.c
	@echo '  Compiling $<...'
	@gcc $(CFLAGS) -o $@ -MMD -MP -c $<
ifdef SPARSE_VERSION
	@sparse $(CFLAGS) $<
endif

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) apfsck
install:
	install -d $(DESTDIR)$(BINDIR)
	install -t $(DESTDIR)$(BINDIR) apfsck
	ln -fs -T apfsck $(DESTDIR)$(BINDIR)/fsck.apfs
	install -d $(DESTDIR)$(MANDIR)
	install -m 644 -t $(DESTDIR)$(MANDIR) apfsck.8
	ln -fs -T apfsck.8 $(DESTDIR)$(MANDIR)/fsck.apfs.8
