#!/usr/bin/python
# vim: fileencoding=utf-8

#
# The Qubes OS Project, https://www.qubes-os.org/
#
# Copyright (C) 2016
#                   Marek Marczykowski-Górecki <marmarek@invisiblethingslab.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
import os
import string
try:
    from qubes.tests.extra import ExtraTestCase
except ImportError:
    # have something working outside of qubes dom0
    import unittest
    ExtraTestCase = unittest.TestCase
import subprocess
import time
import select

try:
    # python3
    from unittest.mock import ANY
except ImportError:
    # python2
    class _ANY(object):
        def __eq__(self, other): return True

        def __ne__(self, other): return False

        def __repr__(self): return '<ANY>'
    ANY = _ANY()
try:
    # for core3 only
    import asyncio
except ImportError:
    pass

keyboard_events = ['KEY_' + x for x in string.ascii_uppercase] +\
    ['KEY_' + x for x in string.digits] +\
    ['KEY_ESC', 'KEY_MINUS', 'KEY_EQUAL', 'KEY_BACKSPACE', 'KEY_TAB',
        'KEY_LEFTBRACE', 'KEY_RIGHTBRACE', 'KEY_ENTER', 'KEY_LEFTCTRL']

mouse_events = ['BTN_LEFT', 'BTN_RIGHT', 'REL_X', 'REL_Y']

# need to extended ABS_* to include min/max values
tablet_events = [
    'BTN_TOUCH',
    'ABS_X + (0, 65535, 0, 0)',
    'ABS_Y + (0, 65535, 0, 0)',
    'ABS_MT_TOOL_X + (0, 1024, 0, 0)',
    'ABS_MT_TOOL_Y + (0, 768, 0, 0)',
    'ABS_PRESSURE + (0, 511, 0, 0)']


class TC_00_InputProxy(ExtraTestCase):
    template = None
    def setUp(self):
        super(TC_00_InputProxy, self).setUp()
        if self.template is not None:
            if self.template.startswith('whonix-'):
                self.skipTest('No network on Whonix VMs during tests - python-uinput not installable')
            # for possible uinput library installation
            self.enable_network()
            self.vm = self.create_vms(["input"])[0]
            self.vm.start(start_guid=False)
            if self.vm.run("python3 -c 'import uinput'", wait=True, gui=False) != 0:
                # If uinput module not installed, try to install it with pip
                p = self.vm.run("pip3 install --break-system-packages git+https://github.com/marmarek/python-uinput@py311 2>&1", passio_popen=True,
                    user="root", gui=False)
                (stdout, _) = p.communicate()
                if p.returncode != 0:
                    self.skipTest("python3-uinput not installed and failed to "
                                  "install it using pip: {}".format(stdout))
            if self.vm.run('modprobe uinput',
                    user="root", wait=True, gui=False) != 0:
                self.skipTest("Failed to load uinput kernel module")
        else:
            try:
                import uinput
            except ImportError:
                self.skipTest('uinput python library not installed')
            self.service_opts = []

    def tearDown(self):
        if hasattr(self, 'event_listener'):
            self.event_listener.terminate()
            self.event_listener.stdout.close()
            self.event_listener.wait()
        if hasattr(self, 'device_pipe'):
            self.destroyDevice()
        if hasattr(self, 'device_proxy'):
            self.device_proxy.wait()
        super(TC_00_InputProxy, self).tearDown()

    def destroyDevice(self):
        self.device_pipe.write(b'dev.destroy()\n')
        self.device_pipe.close()
        if hasattr(self, 'device_proc'):
            self.device_proc.wait()

    def setUpDevice(self, events, name="Test input device", vendor="0x1234"):
        if self.template is not None:
            p = self.vm.run('python3 -i >/dev/null', user="root",
                passio_popen=True, gui=False)
        else:
            with open('/dev/null', 'r+') as null:
                p = subprocess.Popen(['sudo', 'python3', '-i'],
                    stdout=null,
                    stderr=null,
                    stdin=subprocess.PIPE)
        self.device_proc = p
        self.device_pipe = p.stdin
        self.device_pipe.write(b'from uinput import *\n')
        self.device_pipe.write(
            'dev = Device([{}], name="{}", vendor={})\n'.format(
                ','.join(events),
                name,
                vendor,
            ).encode())

        try:
            self.device_pipe.flush()
        except AttributeError:
            self.loop.run_until_complete(self.device_pipe.drain())

        if self.template is None:
            dev_event_path = None
            while not dev_event_path:
                dev_event_path = subprocess.check_output(
                    'ls -d /sys/devices/virtual/input/*/event* 2>/dev/null| '
                    'tail -1',
                    shell=True)
                dev_event_path = dev_event_path.decode().strip()
            dev_event_path = '/dev/input/' + os.path.basename(dev_event_path)

            call_env = os.environ.copy()
            # prefer executables from source directory
            if os.path.exists('src/input-proxy-sender') \
                    and os.path.exists('src/input-proxy-receiver'):
                cmd_prefix = 'src/'
            else:
                cmd_prefix = ''

            call_env['QREXEC_REMOTE_DOMAIN'] = 'remote'
            self.device_proxy = subprocess.Popen([
                'sudo', '-E', 'socat',
                'exec:{}input-proxy-sender {}'.format(cmd_prefix,
                    dev_event_path),
                'exec:{}input-proxy-receiver {}'.format(
                    cmd_prefix,
                    ' '.join(self.service_opts))],
                env=call_env)

    def emit_event(self, event, value):
        self.device_pipe.write('dev.emit({}, {})\n'.format(event, value).encode())
        try:
            self.device_pipe.flush()
        except AttributeError:
            self.loop.run_until_complete(self.device_pipe.drain())

    def emit_click(self, key):
        # don't use dev.emit_click, python-uinput 0.10.1 is buggy (do not
        # send EV_SYN in between - X server driver ignore such events)
        self.device_pipe.write('dev.emit({}, 1)\n'.format(key).encode())
        self.device_pipe.write('dev.emit({}, 0)\n'.format(key).encode())
        try:
            self.device_pipe.flush()
        except AttributeError:
            self.loop.run_until_complete(self.device_pipe.drain())

    def parse_one_event(self):
        event_type = None
        ignore = False
        in_event_data = False
        flags = {}
        detail = '0'
        for line in iter(
                lambda: self.event_listener.stdout.readline(), ''):
            line = line.decode().rstrip()
            if not line:
                break
            if line.startswith('EVENT'):
                # EVENT type 17 (RawMotion)
                event_type = line.split()[3].strip('()')
                ignore = False
            elif ignore:
                pass
            elif not line.startswith('    '):
                # non-event packet - for example device discovery, ignore
                ignore = True
            elif line.startswith('    detail:'):
                detail = line.split(':')[1].strip()
            elif line.startswith('    flags:'):
                in_event_data = True
            elif line.startswith('    valuators:'):
                in_event_data = True
            elif line.startswith('        '):
                #         0: 120.34 (105.00)
                #         1: -25.21 (-22.0)
                if in_event_data:
                    key, value = line.strip().split(':')
                    # use raw value
                    value = value.split()[1].strip('()')
                    # round the value (to combat Xorg's float usage) and
                    # convert back to %.2f (to keep the format)
                    value = '{:.2f}'.format(round(float(value)))
                    flags[key] = value
                else:
                    pass
            elif line.startswith('    '):
                # other fields - ignore
                in_event_data = False

        if ignore:
            # retry
            return None
        # xinput in fc20 didn't included unchanged values
        if '0' in flags and '1' not in flags:
            flags['1'] = '0.00'
        if '1' in flags and '0' not in flags:
            flags['0'] = '0.00'
        return [event_type, detail, flags]

    def assertNoEvent(self, msg=None, timeout=3000):
        # wait for events with timeout
        poll = select.poll()
        poll.register(self.event_listener.stdout.fileno())
        if poll.poll(timeout):
            event = self.parse_one_event()
            if event is None:
                # ignored event
                self.assertNoEvent(msg, timeout)
            else:
                self.fail("Received unexpected {} event{}".format(
                    event,
                    (": " + msg) if msg else ""
                ))

    def get_event(self, timeout=3000):
        # wait for events with timeout
        poll = select.poll()
        poll.register(self.event_listener.stdout.fileno())
        if not poll.poll(timeout):
            self.fail("Didn't received event within 3 sec timeout")
        event = self.parse_one_event()
        if event is None:
            # retry
            return self.get_event(timeout)
        return event

    def assertEvent(self, expected_event, timeout=3000):
        event = self.get_event(timeout)
        self.assertEqual(event, expected_event)

    def find_device_and_start_listener(self, expected_name=None):
        if expected_name is None:
            if hasattr(self, 'vm'):
                expected_name = '{}: {}'.format(
                    self.vm.name, 'Test input device')
            else:
                expected_name = 'remote: Test input device'
        device_id = None
        try_count = 20
        while device_id is None and try_count > 0:
            try:
                with open(os.devnull, 'w') as null:
                    device_id = subprocess.check_output(
                        ['xinput', 'list', '--id-only', expected_name],
                        stderr=null
                    ).strip()
            except subprocess.CalledProcessError:
                try_count -= 1
                if hasattr(self, 'loop'):
                    # core3 uses asyncio
                    self.loop.run_until_complete(asyncio.sleep(0.2))
                else:
                    time.sleep(0.2)

        self.assertIsNotNone(device_id,
            "Device '{}' not found".format(expected_name))
        device_id = device_id.decode()

        # terminate old listener if there was one
        if hasattr(self, 'event_listener'):
            self.event_listener.terminate()
            self.event_listener.stdout.close()
            self.event_listener.wait()
        self.event_listener = subprocess.Popen(
            ['xinput', 'test-xi2', '--root', device_id],
            stdout=subprocess.PIPE, bufsize=0)
        # wait for the listener to really listen for events
        time.sleep(0.5)

    def allow_service(self, service):
        if hasattr(self, 'vm'):
            self.qrexec_policy(service, self.vm.name, 'dom0')
        else:
            if service == 'qubes.InputMouse':
                self.service_opts.append('--mouse')
            elif service == 'qubes.InputTablet':
                self.service_opts.append('--mouse')
                self.service_opts.append('--tablet')
            elif service == 'qubes.InputKeyboard':
                self.service_opts.append('--keyboard')
                self.service_opts.append('--mouse')
            else:
                self.fail('service {} ?!'.format(service))

    def test_000_simple_mouse(self):
        """Plain mouse"""
        self.allow_service('qubes.InputMouse')
        self.setUpDevice(mouse_events)
        self.find_device_and_start_listener()
        self.emit_event('REL_X', 1)
        self.emit_event('REL_X', 1)
        self.emit_event('REL_Y', 1)
        self.emit_event('REL_Y', 1)
        self.emit_click('BTN_LEFT')

        self.assertEvent(['RawMotion', '0', {'0': '1.00', '1': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'0': '1.00', '1': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'1': '1.00', '0': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'1': '1.00', '0': '0.00'}])
        self.assertEvent(['RawButtonPress', '1', {}])
        self.assertEvent(['RawButtonRelease', '1', {}])

    def test_010_mouse_deny_keyboard(self):
        """Mouse trying to send keyboard events"""
        self.allow_service('qubes.InputMouse')
        self.setUpDevice(mouse_events)
        self.find_device_and_start_listener()
        self.emit_event('KEY_A', 1)
        self.emit_event('KEY_B', 1)
        self.emit_event('KEY_C', 1)
        self.emit_event('KEY_D', 1)
        self.assertNoEvent(msg="keyboard should be denied")

    def test_020_mouse_keyboard_mouse_only(self):
        """Mouse and keyboard combined device, but only mouse allowed"""
        self.allow_service('qubes.InputMouse')
        self.setUpDevice(['BTN_LEFT', 'BTN_RIGHT', 'REL_X', 'REL_Y'] + keyboard_events)
        self.find_device_and_start_listener()
        self.emit_event('REL_X', 1)
        self.emit_event('REL_X', 1)
        self.emit_event('REL_Y', 1)
        self.emit_event('REL_Y', 1)
        self.emit_click('BTN_LEFT')

        self.assertEvent(['RawMotion', '0', {'0': '1.00', '1': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'0': '1.00', '1': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'1': '1.00', '0': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'1': '1.00', '0': '0.00'}])
        self.assertEvent(['RawButtonPress', '1', {}])
        self.assertEvent(['RawButtonRelease', '1', {}])

        self.emit_event('KEY_A', 1)
        self.emit_event('KEY_B', 1)
        self.emit_event('KEY_C', 1)
        self.emit_event('KEY_D', 1)
        self.assertNoEvent(msg="keyboard should be denied")

    def test_030_simple_keyboard(self):
        """Just keyboard"""
        self.allow_service('qubes.InputKeyboard')
        self.setUpDevice(keyboard_events)
        self.find_device_and_start_listener()
        self.emit_click('KEY_A')
        self.emit_click('KEY_B')
        self.emit_click('KEY_C')
        self.emit_click('KEY_D')
        for _ in range(4):
            self.emit_click('KEY_BACKSPACE')

        for key in ('38', '56', '54', '40'):
            self.assertEvent(['RawKeyPress', key, {}])
            self.assertEvent(['RawKeyRelease', key, {}])
        for _ in range(4):
            self.assertEvent(['RawKeyPress', '22', {}])
            self.assertEvent(['RawKeyRelease', '22', {}])

    def test_040_mouse_keyboard(self):
        """Mouse and keyboard combined device"""
        self.allow_service('qubes.InputMouse')
        self.allow_service('qubes.InputKeyboard')
        self.setUpDevice(mouse_events + keyboard_events)
        dev_name = '{}: {}'.format(
            self.vm.name if hasattr(self, 'vm') else 'remote',
            'Test input device')
        self.find_device_and_start_listener('pointer:' + dev_name)
        self.emit_event('REL_X', 1)
        self.emit_event('REL_X', 1)
        self.emit_event('REL_Y', 1)
        self.emit_event('REL_Y', 1)
        self.emit_click('BTN_LEFT')

        self.assertEvent(['RawMotion', '0', {'0': '1.00', '1': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'0': '1.00', '1': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'1': '1.00', '0': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'1': '1.00', '0': '0.00'}])
        self.assertEvent(['RawButtonPress', '1', {}])
        self.assertEvent(['RawButtonRelease', '1', {}])

        self.find_device_and_start_listener('keyboard:' + dev_name)

        self.emit_click('KEY_A')
        self.emit_click('KEY_B')
        self.emit_click('KEY_C')
        self.emit_click('KEY_D')
        for _ in range(4):
            self.emit_click('KEY_BACKSPACE')

        for key in ('38', '56', '54', '40'):
            self.assertEvent(['RawKeyPress', key, {}])
            self.assertEvent(['RawKeyRelease', key, {}])
        for _ in range(4):
            self.assertEvent(['RawKeyPress', '22', {}])
            self.assertEvent(['RawKeyRelease', '22', {}])

    def test_050_mouse_late_attach(self):
        """Test reattach at user login/GUI start - #1930"""
        if not hasattr(self, 'vm'):
            self.skipTest('makes no sense without a vm')
        self.setUpDevice(mouse_events)
        time.sleep(2)
        self.allow_service('qubes.InputMouse')
        # trigger GUI startup
        try:
            # R4.0+
            p = self.loop.run_until_complete(
                asyncio.create_subprocess_exec('qvm-start-gui', self.vm.name))
            self.loop.run_until_complete(p.communicate())
        except (FileNotFoundError, AttributeError):
            # R3.2
            pass
        self.vm.run("true", gui=True, wait=True)
        self.find_device_and_start_listener()
        self.emit_event('REL_X', 1)
        self.emit_event('REL_X', 1)
        self.emit_event('REL_Y', 1)
        self.emit_event('REL_Y', 1)
        self.emit_click('BTN_LEFT')

        self.assertEvent(['RawMotion', '0', {'0': '1.00', '1': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'0': '1.00', '1': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'1': '1.00', '0': '0.00'}])
        self.assertEvent(['RawMotion', '0', {'1': '1.00', '0': '0.00'}])
        self.assertEvent(['RawButtonPress', '1', {}])
        self.assertEvent(['RawButtonRelease', '1', {}])

    def test_060_tablet(self):
        """Test tablet events (absolute axis)"""
        self.allow_service('qubes.InputTablet')
        # try:
        #     root_info = subprocess.check_output(['xwininfo', '-root']).decode()
        #     for line in root_info.splitlines():
        #         if 'Width:' in line:
        #             _, _, width = line.partition('Width: ')
        #         elif 'Height:' in line:
        #             _, _, height = line.partition('Height: ')
        #     tablet_events[1] = 'ABS_X + (0, {}, 0, 0)'.format(width)
        #     tablet_events[2] = 'ABS_Y + (0, {}, 0, 0)'.format(height)
        #     tablet_events[3] = 'ABS_MT_TOOL_X + (0, {}, 0, 0)'.format(width)
        #     tablet_events[4] = 'ABS_MT_TOOL_Y + (0, {}, 0, 0)'.format(height)
        # except subprocess.CalledProcessError:
        #     pass

        self.setUpDevice(tablet_events)
        self.find_device_and_start_listener()

        self.emit_event('ABS_X', 15000)
        self.emit_event('ABS_Y', 15000)
        self.emit_event('BTN_TOUCH', 1)
        self.emit_event('ABS_X', 16000)
        self.emit_event('ABS_Y', 16000)
        self.emit_event('BTN_TOUCH', 0)
        # should be ignored
        self.emit_event('REL_Y', 1)
        self.emit_event('REL_Y', 1)

        self.assertEvent(['RawTouchBegin', ANY,
            {'0': '15000.00', '1': '15000.00'}])
        self.assertEvent(['RawTouchUpdate', ANY,
            {'0': '16000.00', '1': '15000.00'}])
        self.assertEvent(['RawTouchUpdate', ANY,
            {'0': '16000.00', '1': '16000.00'}])
        # FIXME: really (0, 0)?
        self.assertEvent(['RawTouchEnd', ANY, {'0': '0.00', '1': '0.00'}])
        self.assertNoEvent(msg="rel events should be ignored")

class TC_01_InputProxyExclude(ExtraTestCase):
    template = None
    def setUp(self):
        super(TC_01_InputProxyExclude, self).setUp()
        if self.template is None:
            self.skipTest('skip outside real qubes')
        if self.template.startswith('whonix-'):
            self.skipTest('No input proxy on whonix')

    def find_device(self):
        try_count = 20
        for try_no in range(try_count):
            try:
                xinput_list = subprocess.check_output(
                    ['xinput', 'list', '--name-only'],
                    stderr=subprocess.DEVNULL
                )
                for line in xinput_list.decode().splitlines():
                    if line.startswith(self.vm.name + ':'):
                        return line
            except subprocess.CalledProcessError:
                pass
            self.loop.run_until_complete(asyncio.sleep(0.2))

        return None

    def test_000_qemu_tablet(self):
        self.vm = self.create_vms(["input"])[0]
        self.vm.virt_mode = 'hvm'
        self.qrexec_policy('qubes.InputMouse', self.vm.name, 'dom0')
        self.qrexec_policy('qubes.InputTablet', self.vm.name, 'dom0')
        self.vm.start(start_guid=False)
        p = self.loop.run_until_complete(
            asyncio.create_subprocess_exec('qvm-start-gui', self.vm.name))
        self.loop.run_until_complete(p.communicate())
        device = self.find_device()
        self.assertIsNone(device,
            "QEMU device not filtered out: {!r}".format(device))


def list_tests():
    return (
        TC_00_InputProxy,
        TC_01_InputProxyExclude,
    )
