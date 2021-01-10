LDFLAGS=-lX11 -lXrandr -lm -lXi
CFLAGS=-Os

PROJECTS=autorotate ltSwitch

all:$(PROJECTS)
clean:
	rm -f $(PROJECTS)
