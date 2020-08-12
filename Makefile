LDFLAGS=-lX11 -lXrandr -lm -lXi
CFLAGS=-O0 -ggdb

PROJECTS=autorotate

all:$(PROJECTS)
clean:
	rm -f $(PROJECTS)
