#!/usr/bin/env sh
gcc main.c \
	-Werror \
	-Wall \
	$(pkg-config --cflags freetype2) \
	-l bcm2835 \
	$(pkg-config --libs freetype2) \
	-o unicorn
