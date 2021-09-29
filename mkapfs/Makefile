SRCS = btree.c dir.c mkapfs.c object.c spaceman.c super.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

LIBDIR = ../lib
LIBRARY = $(LIBDIR)/libapfs.a

BINDIR = ~/bin
MANDIR = ~/share/man/man8

SPARSE_VERSION := $(shell sparse --version 2>/dev/null)

override CFLAGS += -Wall -Wno-address-of-packed-member -fno-strict-aliasing -I$(CURDIR)/../include

mkapfs: $(OBJS) $(LIBRARY)
	@echo '  Linking...'
	@gcc $(CFLAGS) $(LDFLAGS) -o mkapfs $(OBJS) $(LIBRARY)
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
	rm -f $(OBJS) $(DEPS) mkapfs
install:
	install -d $(BINDIR)
	install -t $(BINDIR) mkapfs
	ln -fs -T $(BINDIR)/mkapfs $(BINDIR)/mkfs.apfs
	install -d $(MANDIR)
	install -m 644 -t $(MANDIR) mkapfs.8
	ln -fs -T $(MANDIR)/mkapfs.8 $(MANDIR)/mkfs.apfs.8
