PYTHON ?= python3

all:
	$(MAKE) -C src all

install: install-vm install-dom0

install-vm:
	$(MAKE) -C src install
	$(MAKE) -C qubes-rpc install-vm

install-dom0:
	$(MAKE) -C qubes-rpc install-dom0
	$(PYTHON) setup.py install -O1 --root $(DESTDIR)

clean:
	$(MAKE) -C src clean
