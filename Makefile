#
# libwss (Web Socket Server) library
#
# Copyright (C) 2023, Naveen Albert
#

CC		= gcc

# Detect macOS and conditionally exclude GCC-specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	# macOS uses Clang even when gcc is called, so exclude GCC-specific flags
	CFLAGS = -Wall -Werror -Wunused -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wdeclaration-after-statement -Wmissing-declarations -Wmissing-format-attribute -Wnull-dereference -Wformat=2 -Wshadow -Wsizeof-pointer-memaccess -std=gnu99 -pthread -O3 -g -Wstack-protector -fno-omit-frame-pointer -fwrapv -fPIC -D_FORTIFY_SOURCE=2
else
	# Non-macOS systems (Linux, etc.) with real GCC
	CFLAGS = -Wall -Werror -Wunused -Wextra -Wmaybe-uninitialized -Wstrict-prototypes -Wmissing-prototypes -Wdeclaration-after-statement -Wmissing-declarations -Wmissing-format-attribute -Wnull-dereference -Wformat=2 -Wshadow -Wsizeof-pointer-memaccess -std=gnu99 -pthread -O3 -g -Wstack-protector -fno-omit-frame-pointer -fwrapv -fPIC -D_FORTIFY_SOURCE=2
endif
EXE		= wss
LIBNAME = libwss
RM		= rm -f
INSTALL = install

MAIN_SRC := wss.c
MAIN_OBJ = $(MAIN_SRC:.c=.o)

# Use $(MAIN_OBJ) instead of $^ for portability to BSD make
all: $(MAIN_OBJ)
	@echo "== Linking $(MAIN_OBJ)"
	$(CC) -shared -fPIC -o $(LIBNAME).so $(MAIN_OBJ)

install: all
	$(INSTALL) -m  755 $(LIBNAME).so "/usr/lib/"
	$(INSTALL) -m 755 $(EXE).h "/usr/include"

tests: test.o
	$(CC) $(CFLAGS) -o test *.o -lwss

uninstall:
	$(RM) /usr/lib/$(LIBNAME).so
	$(RM) /usr/include/$(EXE).h

# Use SUFFIXES instead of pattern rules, which BSD make doesn't support
.SUFFIXES:
.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) -c $<

clean :
	$(RM) *.i *.o *.so $(EXE)

.PHONY: all
.PHONY: install
.PHONY: uninstall
.PHONY: clean
