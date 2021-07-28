CFLAGS ?= -Wall -Wextra -Os -flto
LDFLAGS ?= -Wl,--gc-sections
target := rstparser

ifeq ($(OS),Windows_NT)
    target := $(target).exe
endif

all: $(target)

objects := getline.o rstparser.o

$(target): $(objects)
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -f $(target) $(objects)
