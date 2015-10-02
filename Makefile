
LIBS := -lm

CFLAGS := -DGETTIMEOFDAY_TWO_ARGS

all: libanalogtv.so

libanalogtv.so: analogtv.o aligned_malloc.o thread_util.o yarandom.o
	$(CC) $(LIBS) -o $@.out $^

# = Generic Utility Definitions =

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm *.o
	rm *.out

