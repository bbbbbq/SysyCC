CC ?= cc
AR ?= ar
CFLAGS ?= -O0 -pipe
CPPFLAGS ?= -I. -isystem ./sys
OBJ = dep_add.o dep_mul.o

all: libdepdemo.a

libdepdemo.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MD -MF $@.d -MQ $@ -MP -c $< -o $@

-include $(OBJ:%=%.d)

clean:
	rm -f $(OBJ) $(OBJ:%=%.d) libdepdemo.a
