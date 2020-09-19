#!/usr/bin/env sh
gcc main.c \
	-Werror \
	-Wall \
	$(pkg-config --cflags freetype2) \
	$(pkg-config --libs freetype2) \
	-o unicorn
