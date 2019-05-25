SRCS = checksum.c unicode.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

SPARSE_VERSION := $(shell sparse --version 2>/dev/null)

override CFLAGS += -Wall -I$(CURDIR)/../include

all: $(OBJS)

%.o: %.c
	@echo '  Compiling $<...'
	@gcc $(CFLAGS) -o $@ -MMD -MP -c $<
ifdef SPARSE_VERSION
	@sparse $(CFLAGS) $<
endif

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS)
