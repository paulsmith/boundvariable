all: build

.PHONY: build
build:
	./build.sh

.PHONY: clean
clean:
	-rm um-32
