.POSIX:

all: app point

app: main.c simplex.c simplex.h
	cc -std=c11 -g -o app main.c simplex.c -lglfw -lepoxy
