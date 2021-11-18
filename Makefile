all: a.out

a.out: 
	gcc webserver.c

clean:
	rm a.out