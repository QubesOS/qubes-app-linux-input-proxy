install:
	install -d $(DESTDIR)/usr/lib/systemd/system
	install -m 0644 \
		qubes-input-sender-keyboard@.service \
		qubes-input-sender-keyboard-mouse@.service \
		qubes-input-sender-mouse@.service \
		$(DESTDIR)/usr/lib/systemd/system
	install -d $(DESTDIR)/lib/udev/rules.d
	install -m 0644 qubes-input-proxy.rules \
		$(DESTDIR)/lib/udev/rules.d/90-qubes-input-proxy.rules
	install -m 0644 qubes-uinput.rules \
		$(DESTDIR)/lib/udev/rules.d/90-qubes-uinput.rules
	install -m 0644 -D qubes-uinput.modules \
		$(DESTDIR)/lib/modules-load.d/qubes-uinput.conf
	install -d $(DESTDIR)/etc/qubes-rpc
	install -m 0644 qubes.InputMouse $(DESTDIR)/etc/qubes-rpc
	install -m 0644 qubes.InputKeyboard $(DESTDIR)/etc/qubes-rpc
	install -m 0664 -D qubes.InputMouse.policy \
		$(DESTDIR)/etc/qubes-rpc/policy/qubes.InputMouse
	install -m 0664 -D qubes.InputKeyboard.policy \
		$(DESTDIR)/etc/qubes-rpc/policy/qubes.InputKeyboard
	install -m 0755 -D qubes-input-trigger \
		$(DESTDIR)/usr/bin/qubes-input-trigger
	install -m 0644 -D qubes-input-trigger.desktop \
		$(DESTDIR)/etc/xdg/autostart/qubes-input-trigger.desktop
	install -m 0600 -D qubes-input-trigger.sudoers \
		$(DESTDIR)/etc/sudoers.d/qubes-input-trigger
