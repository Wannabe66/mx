# Makefile to build static libraries.

all: buildlib

buildlib:
	-@find . -mindepth 1 -maxdepth 1 -type d -not -name "ports" -exec $(MAKE) -s -C {} \;
