.POSIX:

all: app

app: main.c simplex.c simplex.h
	cc -std=c11 -g -o app main.c simplex.c -lglfw -lOpenGL
