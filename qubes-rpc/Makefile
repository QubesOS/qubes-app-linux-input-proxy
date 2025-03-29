install: install-vm install-dom0

LIBDIR ?= /lib
USRLIBDIR ?= /usr/lib

install-dom0:
	install -m 0664 -D qubes.InputMouse.policy \
		$(DESTDIR)/etc/qubes-rpc/policy/qubes.InputMouse
	install -m 0664 -D qubes.InputKeyboard.policy \
		$(DESTDIR)/etc/qubes-rpc/policy/qubes.InputKeyboard
	install -m 0664 -D qubes.InputTablet.policy \
		$(DESTDIR)/etc/qubes-rpc/policy/qubes.InputTablet

install-vm:
	install -d $(DESTDIR)$(USRLIBDIR)/systemd/system
	install -m 0644 \
		qubes-input-sender-keyboard@.service \
		qubes-input-sender-keyboard-mouse@.service \
		qubes-input-sender-mouse@.service \
		qubes-input-sender-tablet@.service \
		$(DESTDIR)$(USRLIBDIR)/systemd/system
	install -d $(DESTDIR)$(USRLIBDIR)/udev/rules.d
	install -m 0644 qubes-input-proxy.rules \
		$(DESTDIR)$(USRLIBDIR)/udev/rules.d/90-qubes-input-proxy.rules
	install -m 0644 qubes-uinput.rules \
		$(DESTDIR)$(USRLIBDIR)/udev/rules.d/90-qubes-uinput.rules
	install -m 0644 -D qubes-uinput.modules \
		$(DESTDIR)$(USRLIBDIR)/modules-load.d/qubes-uinput.conf
	install -d $(DESTDIR)/etc/qubes-rpc
	install qubes.InputMouse $(DESTDIR)/etc/qubes-rpc
	install qubes.InputKeyboard $(DESTDIR)/etc/qubes-rpc
	install qubes.InputTablet $(DESTDIR)/etc/qubes-rpc
	install -m 0755 -D qubes-input-trigger \
		$(DESTDIR)/usr/bin/qubes-input-trigger
	install -m 0755 -D qubes-input-sender \
		$(DESTDIR)/usr/bin/qubes-input-sender
	install -m 0644 -D qubes-input-trigger.desktop \
		$(DESTDIR)/etc/xdg/autostart/qubes-input-trigger.desktop
	mkdir -p -m 0750 $(DESTDIR)/etc/sudoers.d
	install -m 0600 qubes-input-trigger.sudoers \
		$(DESTDIR)/etc/sudoers.d/qubes-input-trigger
