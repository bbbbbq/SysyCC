CC ?= cc
AR ?= ar
CFLAGS ?= -O0 -pipe -Winvalid-pch -ffunction-sections -fdata-sections -fno-common -fvisibility=hidden
CPPFLAGS ?= -I.
DEPFLAGS ?= -MMD -MP
OBJ = add.o mul.o

all: libdemo.a

libdemo.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(OBJ:.o=.d)

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) libdemo.a
