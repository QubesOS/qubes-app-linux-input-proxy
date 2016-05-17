
all:
	$(MAKE) -C src all

install:
	$(MAKE) -C src install
	$(MAKE) -C qubes-rpc install
	python setup.py install -O1 --root $(DESTDIR)

clean:
	$(MAKE) -C src clean
