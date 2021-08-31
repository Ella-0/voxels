.POSIX:

all: app

app: main.c simplex.c simplex.h voct.c voct.h
	cc -std=c11 -g -o app main.c simplex.c voct.c -lglfw -lOpenGL -lpthread
