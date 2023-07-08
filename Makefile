#
# libwss (Web Socket Server) library
#
# Copyright (C) 2023, Naveen Albert
#

CC		= gcc
CFLAGS = -Wall -Werror -Wunused -Wextra -Wmaybe-uninitialized -Wstrict-prototypes -Wmissing-prototypes -Wdeclaration-after-statement -Wmissing-declarations -Wmissing-format-attribute -Wnull-dereference -Wformat=2 -Wshadow -Wsizeof-pointer-memaccess -std=gnu99 -pthread -O3 -g -Wstack-protector -fno-omit-frame-pointer -fwrapv -fPIC -D_FORTIFY_SOURCE=2
EXE		= wss
LIBNAME = libwss
RM		= rm -f
INSTALL = install
INSTALL = install

MAIN_SRC := wss.c
MAIN_OBJ = $(MAIN_SRC:.c=.o)

all: $(MAIN_OBJ)
	@echo "== Linking $@"
	$(CC) -shared -fPIC -o $(LIBNAME).so $^

install: all
	$(INSTALL) -m  755 $(LIBNAME).so "/usr/lib/"
	$(INSTALL) -m 755 $(EXE).h "/usr/include"

tests: test.o
	$(CC) $(CFLAGS) -o test *.o -lwss

uninstall:
	$(RM) /usr/lib/$(LIBNAME).so
	$(RM) /usr/include/$(EXE).h

%.o : %.c
	$(CC) $(CFLAGS) -c $^

clean :
	$(RM) *.i *.o *.so $(EXE)

.PHONY: all
.PHONY: install
.PHONY: uninstall
.PHONY: clean
