# Root Makefile for XPilot Classic

all: linux

linux:
	$(MAKE) -C src -f Makefile.std linux

client:
	$(MAKE) -C src -f Makefile.std make-client

server:
	$(MAKE) -C src -f Makefile.std make-server

replay:
	$(MAKE) -C src -f Makefile.std make-replay

mapedit:
	$(MAKE) -C src -f Makefile.std make-mapedit

clean:
	$(MAKE) -C src -f Makefile.std clean

.PHONY: all linux client server replay mapedit clean
