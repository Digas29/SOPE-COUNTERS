CC = gcc
CFLAGS = -Wall -pthread -lrt
LDFLAGS = -Wall
SOURCES = balcao.c ger_cl.c
OBJECTS = $(SOURCES:.c=.o)
EXECUTABLES = $(OBJECTS:.o=)
OUTFOLDER = bin

.PHONY: all


all: makedir $(EXECUTABLES)

.c:
	$(CC) $< -o $(OUTFOLDER)/$@ $(CFLAGS)
makedir:
	mkdir -p $(OUTFOLDER)
