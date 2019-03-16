SRCS = apfsck.c btree.c crc32c.c dir.c extents.c htable.c \
       inode.c key.c object.c super.c unicode.c xattr.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

BINDIR = ~/bin
MANDIR = ~/share/man/man8

apfsck: $(OBJS)
	gcc $(CFLAGS) -o apfsck $(OBJS)

%.o: %.c
	gcc $(CFLAGS) -o $@ -MMD -MP -c $<

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
