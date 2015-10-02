
all: libanalogtv.so

libanalogtv.so: analogtv.o
	$(CC) $(CFLAGS) -o $@.out $^

# = Generic Utility Definitions =

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm *.o
	rm *.out

