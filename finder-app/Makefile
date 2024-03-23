# the := is required to expand the variable emediately and not only after parsing
CC := gcc
SRC = *.c
OBJECT = $(SRC:.c=.o)
TARGET = writer
FLAGS = -Wall -O2

# override CC variable
ifdef CROSS_COMPILE
# the := is required to expand the variable emediately and not only after parsing
# this prevents the error of referencing the variable recursively in itself
	CC := $(CROSS_COMPILE)$(CC)
endif


all: $(TARGET)

$(TARGET): $(OBJECT)
	@$(CC) $^ $(FLAGS) -o $@

%.o: %.c
	@$(CC) -c $^ $(FLAGS) -o $@


.PHONY: clean
clean:
	@rm -f *.o writer