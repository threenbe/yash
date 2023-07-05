# build an executable named myprog from myprog.c
all: yash.c 
	gcc -g -Wall -o yash yash.c -lreadline

clean: 
	$(RM) yash