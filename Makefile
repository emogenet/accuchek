
.PHONY:all clean
SHELL = /bin/bash
LIBS= -lusb-1.0
#CFLAGS=-O0 -g3 -march=native
CFLAGS=-g0 -O3 -march=native -fomit-frame-pointer -DNDEBUG

all: accuchek
	@echo done.

# target accuchek
# ---------------

.objs/main.o:main.cpp
	@echo c++ -- main.cpp
	@mkdir -p .deps
	@mkdir -p .objs
	@g++ -std=c++17 -MD ${CFLAGS} -I. -c main.cpp -o .objs/main.o
	@mv .objs/main.d .deps

.objs/log.o:log.cpp
	@echo c++ -- log.cpp
	@mkdir -p .deps
	@mkdir -p .objs
	@g++ -std=c++17 -MD ${CFLAGS} -I. -c log.cpp -o .objs/log.o
	@mv .objs/log.d .deps

accuchek:.objs/main.o .objs/log.o 
	@echo lnk -- accuchek
	@g++ -std=c++17 ${CFLAGS} -o accuchek .objs/main.o .objs/log.o  -lusb-1.0 -lm

# target clean
# ------------
clean:
	rm -r -f accuchek
	rm -r -f .deps .objs

-include .deps/*


