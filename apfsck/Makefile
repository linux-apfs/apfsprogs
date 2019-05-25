SRCS = apfsck.c btree.c dir.c extents.c htable.c \
       inode.c key.c object.c spaceman.c super.c xattr.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

LIBDIR = ../lib
LIBS = $(LIBDIR)/crc32c.o $(LIBDIR)/unicode.o

BINDIR = ~/bin
MANDIR = ~/share/man/man8

SPARSE_VERSION := $(shell sparse --version 2>/dev/null)

override CFLAGS += -Wall -I$(CURDIR)/../include

apfsck: $(OBJS) libraries
	@echo '  Linking...'
	@gcc $(CFLAGS) -o apfsck $(OBJS) $(LIBS)
	@echo '  Build complete'

# Build the common libraries
libraries: FORCE
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
	install -d $(BINDIR)
	install -t $(BINDIR) apfsck
	ln -fs -T $(BINDIR)/apfsck $(BINDIR)/fsck.apfs
	install -d $(MANDIR)
	install -m 644 -t $(MANDIR) apfsck.8
	ln -fs -T $(MANDIR)/apfsck.8 $(MANDIR)/fsck.apfs.8
