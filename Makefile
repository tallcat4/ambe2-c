# ambe2-c: build and test the encoder and the decoder.   make; make test
all:
	$(MAKE) -C encoder
	$(MAKE) -C decoder

test: all
	$(MAKE) -C encoder test
	$(MAKE) -C decoder test

clean:
	$(MAKE) -C encoder clean
	$(MAKE) -C decoder clean

.PHONY: all test clean
