SRCS = apfsck.c btree.c key.c object.c super.c unicode.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

apfsck: $(OBJS)
	gcc $(CFLAGS) -o apfsck $(OBJS)

%.o: %.c
	gcc $(CFLAGS) -o $@ -MMD -MP -c $<

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) apfsck
