CC     = gcc
CFLAGS = -std=c99 -g -Wall -fsanitize=address,undefined

all: mysh 

mysh: mysh.o
	$(CC) $(CFLAGS) $^ -o $@

abc: abc.o
	$(CC) $(CFLAGS) $^ -o $@

reverse: reverse.o
	$(CC) $(CFLAGS) $^ -o $@

addition: addition.o
	$(CC) $(CFLAGS) $^ -o $@

mysh.o: mysh.c
	$(CC) -c $(CFLAGS) $< -o $@

abc.o: abc.c
	$(CC) -c $(CFLAGS) $< -o $@

reverse.o: reverse.c
	$(CC) -c $(CFLAGS) $< -o $@

addition.o: addition.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf *.o all
