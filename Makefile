LDFLAGS=-lX11 -lXrandr -lm -lXi
CFLAGS=-Os

PROJECTS=autorotate

all:$(PROJECTS)
clean:
	rm -f $(PROJECTS)
