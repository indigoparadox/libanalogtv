
#LIBS := -lm -lalleg -lX11
LIBS := -lm -lX11

CFLAGS := -DGETTIMEOFDAY_TWO_ARGS -DDEBUG -DSAFETY -DX11 -fPIC

all: libanalogtv.so analogtest

analogtest: analogtest.o
	$(CC) -L. -o $@ $^ -lanalogtv $(LIBS)

libanalogtv.so: analogtv.o aligned_malloc.o thread_util.o yarandom.o visual.o
	$(CC) $(LIBS) -shared -o $@ $^

# = Generic Utility Definitions =

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm *.o
	rm *.so

