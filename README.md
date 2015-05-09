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

You need to have access to the device file, which most likely need that
`input-proxy-sender` should be started as `root`, or you need some `udev` rule
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

Example usage
-------------

The simplest way to use it is to just call `qvm-run`:

    qvm-run -u root --pass-io --localcmd="input-proxy-receiver --mouse" usbvm "input-proxy-sender /dev/input/event2"


Alternatively you can allow USB VM to initiate the connection whenever you
attach your device. For that you need to create Qubes RPC service which will
start `input-proxy-receiver`, for example `/etc/qubes-rpc/qubes.InputMouse`:

    input-proxy-receiver --mouse

Provide appropriate policy `/etc/qubes-rpc/policy/qubes.InputMouse`:

    usbvm dom0 allow,user=root

Then create systemd service in your USB VM, which will call
`input-proxy-sender`, for example
`/etc/systemd/system/input-proxy-sender.service:

    [Unit]
    Name=Input proxy sender
    After=qubes-qrexec-agent.service

    [Service]
    ExecStart=/usr/bin/qrexec-client-vm dom0 qubes.InputMouse /usr/bin/input-proxy-sender /dev/input/event2


And create udev rule in your USB VM, which will automatically start the
service, for example `/etc/udev/rules.d/input-proxy.rules`:

    KERNEL=="input/event2", ACTION=="add", RUN+="/bin/systemctl --no-block start input-proxy-sender.service"
    KERNEL=="input/event2", ACTION=="remove", RUN+="/bin/systemctl --no-block stop input-proxy-sender.service"
