SRCS = apfsck.c btree.c crc32c.c dir.c extents.c htable.c \
       inode.c key.c object.c spaceman.c super.c unicode.c xattr.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

BINDIR = ~/bin
MANDIR = ~/share/man/man8

SPARSE_VERSION := $(shell sparse --version 2>/dev/null)

override CFLAGS += -Wall -I$(CURDIR)/../include

apfsck: $(OBJS)
	@echo '  Linking...'
	@gcc $(CFLAGS) -o apfsck $(OBJS)
	@echo '  Build complete'

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
