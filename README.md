Simple input events proxy
=========================

This package consists of two tools:
1. input-proxy-sender
2. input-proxy-receiver

The first one is intended to run in the VM with physical input device attached
(for example USB VM).

The second one is intended to run in Dom0 (or GUI VM when implemented). It will
receive the events and pass them to emulated device.


input-proxy-sender
------------------

The only option is device file path, for example `/dev/input/event2`. Take a
look at `/dev/input/by-id` directory for stable named symlinks. You should use
`event` devices, not `mouse` or anything else.

You need to have access to the device file, which most likely means that you
need to start `input-proxy-sender` as `root`, or you need some `udev` rule
to alter file permissions.

input-proxy-receiver
--------------------

This part performs events filtering according to desired device type. The tool
has options to specify allowed event types:

* `--mouse` - allow events specific for mouses (`EV_REL` and `EV_KEY` with
        `BTN_LEFT`, `BTN_MIDDLE`, `BTN_RIGHT`)
* `--keyboard` - allow events specific for keyboards (`EV_KEY`, `EV_LED`)
* `--tablet` - allow events specific for tablets/touchscreens (`EV_ABS`)

Additionally you can specify how emulated device should look like:

* `--name` - device name (string up to 80 chars)
* `--vendor` - vendor ID
* `--product` - product ID

This tool uses `/dev/uinput` to emulate the device, which means you need:
1. Load `uinput` kernel module.
2. Set appropriate permissions, so you'll have access to it (or start the tool as root).


Security notice
---------------

Keep in mind that VM to which your input devices are connected, has
effectively control over your system. Because of this, benefits of using USB VM
then are much smaller than using fully untrusted USB VM. Besides having control
over your system, such VM can also sniff all the input your are entering there
- for example passwords in case of keyboard.

There is no simple way to protect against sniffing. But you can make it harder
to exploit control over input devices.

If you have only mouse connected to USB VM, but keyboard is still in dom0
(using PS/2 connector for example), you can simply need to lock the screen when
you are away from keyboard. Every time, even if no other person could access
your computer. This is because you are guarding the system not only against
other person, but also against possible actions from USB VM.

If your keyboard is also connected to USB VM, things are much harder. Locking
the screen (with traditional password) does not solve the problem, because USB
VM can simply sniff this password and later easily unlock the screen. One
possibility is to setup screen locker to require some additional step to
unlock. Generally some physical presence factor in addition to password. One
way to achieve this is to use [YubiKey](https://www.qubes-os.org/doc/YubiKey/),
or some other hardware token. Or even manually entered one time password.

Packages
--------

There are two packages available:
1. qubes-input-proxy-sender - to be installed in VM with actual input device
2. qubes-input-proxy - to be installed in dom0

Those packages already contains useful service files. The only thing you need
to do, is to setup appropriate policy in
`/etc/qubes-rpc/policy/qubes.InputMouse` (and maybe also
`qubes.InputKeyboard`). Default policy dany any access.

Manual usage
------------

You can also manually use this proxy. The simplest way to do that, is to just
call `qvm-run`:

    qvm-run -u root --pass-io --localcmd="input-proxy-receiver --mouse" usbvm "input-proxy-sender /dev/input/event2"


Alternatively you can allow USB VM to initiate the connection whenever you
attach your device. For that you need to create Qubes RPC service which will
start `input-proxy-receiver`, for example `/etc/qubes-rpc/qubes.InputMouse`:

    input-proxy-receiver --mouse

Provide appropriate policy `/etc/qubes-rpc/policy/qubes.InputMouse`:

    usbvm dom0 allow,user=root

Then create systemd service in your USB VM, which will call
`input-proxy-sender`, for example
`/etc/systemd/system/input-sender-mouse@.service`:

    [Unit]
    Name=Input proxy sender
    After=qubes-qrexec-agent.service

    [Service]
    ExecStart=/usr/bin/qrexec-client-vm dom0 qubes.InputMouse /usr/bin/input-proxy-sender /dev/input/%i


And create udev rule in your USB VM, which will automatically start the
service, for example `/etc/udev/rules.d/input-proxy.rules`:

    KERNEL=="event*", ACTION=="add", ENV{ID_INPUT_MOUSE}=="1", RUN+="/bin/systemctl --no-block start input-sender-mouse@%k.service"
    KERNEL=="event*", ACTION=="remove", RUN+="/bin/systemctl --no-block stop input-sender-mouse@%k.service"
