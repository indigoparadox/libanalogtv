
LIBS := -lm -lalleg -lX11

CFLAGS := -DGETTIMEOFDAY_TWO_ARGS -DX11 -fPIC

all: libanalogtv.so analogtest

analogtest: analogtest.o
	$(CC) $(LIBS) -L. -lanalogtv -o $@ $^

libanalogtv.so: analogtv.o aligned_malloc.o thread_util.o yarandom.o visual.o
	$(CC) $(LIBS) -shared -o $@ $^

# = Generic Utility Definitions =

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm *.o
	rm *.so

