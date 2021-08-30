.POSIX:

all: app point

app: main.c simplex.c simplex.h
	cc -std=c11 -g -o app main.c simplex.c -lglfw -lepoxy

point: point.c
	cc -std=c11 -g -o point point.c -lglfw -lepoxy
