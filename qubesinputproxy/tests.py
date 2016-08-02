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
import qubes.tests.extra
import subprocess
import time
import select

keyboard_events = ['KEY_' + x for x in string.uppercase] +\
    ['KEY_' + x for x in string.digits] +\
    ['KEY_ESC', 'KEY_MINUS', 'KEY_EQUAL', 'KEY_BACKSPACE', 'KEY_TAB',
        'KEY_LEFTBRACE', 'KEY_RIGHTBRACE', 'KEY_ENTER', 'KEY_LEFTCTRL']

mouse_events = ['BTN_LEFT', 'BTN_RIGHT', 'REL_X', 'REL_Y']


class TC_00_InputProxy(qubes.tests.extra.ExtraTestCase):
    def setUp(self):
        super(TC_00_InputProxy, self).setUp()
        if self.template.startswith('whonix-'):
            self.skipTest('No network on Whonix VMs during tests - python-uinput not installable')
        self.vm = self.create_vms(["input"])[0]
        self.vm.start(start_guid=False)
        if self.vm.run("python -c 'import uinput'", wait=True, gui=False) != 0:
            # If uinput module not installed, try to install it with pip
            p = self.vm.run("pip install python-uinput 2>&1", passio_popen=True,
                user="root", gui=False)
            (stdout, _) = p.communicate()
            if p.returncode != 0:
                self.skipTest("python-uinput not installed and failed to "
                              "install it using pip: {}".format(stdout))
        if self.vm.run('modprobe uinput',
                user="root", wait=True, gui=False) != 0:
            self.skipTest("Failed to load uinput kernel module")

    def tearDown(self):
        if hasattr(self, 'event_listener'):
            self.event_listener.terminate()
        super(TC_00_InputProxy, self).tearDown()

    def destroyDevice(self):
        self.device_pipe.write('dev.destroy()\n')
        self.device_pipe.close()

    def setUpDevice(self, events, name="Test input device", vendor="0x1234"):
        p = self.vm.run('python -i >/dev/null', user="root",
            passio_popen=True, gui=False)
        self.device_pipe = p.stdin
        self.device_pipe.write('from uinput import *\n')
        self.device_pipe.write(
            'dev = Device([{}], name="{}", vendor={})\n'.format(
                ','.join(events),
                name,
                vendor,
            ))

    def emit_event(self, event, value):
        self.device_pipe.write('dev.emit({}, {})\n'.format(event, value))

    def emit_click(self, key):
        self.device_pipe.write('dev.emit_click({})\n'.format(key))

    def parse_one_event(self):
        event_type = None
        ignore = False
        in_event_data = False
        flags = {}
        detail = '0'
        for line in iter(
                lambda: self.event_listener.stdout.readline().rstrip(), ''):
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
            self.fail("Didn't received motion event within 3 sec timeout")
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
            expected_name = '{}: {}'.format(self.vm.name, 'Test input device')
        device_id = None
        try_count = 20
        while device_id is None and try_count > 0:
            try:
                device_id = subprocess.check_output(
                    ['xinput', 'list', '--id-only', expected_name],
                    stderr=open(os.devnull, 'w')
                ).strip()
            except subprocess.CalledProcessError:
                try_count -= 1
                time.sleep(0.2)

        self.assertIsNotNone(device_id,
            "Devive '{}' not found".format(expected_name))

        # terminate old listener if there was one
        if hasattr(self, 'event_listener'):
            self.event_listener.terminate()
        self.event_listener = subprocess.Popen(
            ['xinput', 'test-xi2', '--root', device_id],
            stdout=subprocess.PIPE)
        # wait for the listener to really listen for events
        time.sleep(0.5)

    def test_000_simple_mouse(self):
        """Plain mouse"""
        self.qrexec_policy('qubes.InputMouse', self.vm.name, 'dom0')
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
        self.qrexec_policy('qubes.InputMouse', self.vm.name, 'dom0')
        self.setUpDevice(mouse_events)
        self.find_device_and_start_listener()
        self.emit_event('KEY_A', 1)
        self.emit_event('KEY_B', 1)
        self.emit_event('KEY_C', 1)
        self.emit_event('KEY_D', 1)
        self.assertNoEvent(msg="keyboard should be denied")

    def test_020_mouse_keyboard_mouse_only(self):
        """Mouse and keyboard combined device, but only mouse allowed"""
        self.qrexec_policy('qubes.InputMouse', self.vm.name, 'dom0')
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
        self.qrexec_policy('qubes.InputKeyboard', self.vm.name, 'dom0')
        self.setUpDevice(keyboard_events)
        self.find_device_and_start_listener()
        self.emit_click('KEY_A')
        self.emit_click('KEY_B')
        self.emit_click('KEY_C')
        self.emit_click('KEY_D')
        for _ in xrange(4):
            self.emit_click('KEY_BACKSPACE')

        for key in ('38', '56', '54', '40'):
            self.assertEvent(['RawKeyPress', key, {}])
            self.assertEvent(['RawKeyRelease', key, {}])
        for _ in xrange(4):
            self.assertEvent(['RawKeyPress', '22', {}])
            self.assertEvent(['RawKeyRelease', '22', {}])

    def test_040_mouse_keyboard(self):
        """Mouse and keyboard combined device"""
        self.qrexec_policy('qubes.InputMouse', self.vm.name, 'dom0')
        self.qrexec_policy('qubes.InputKeyboard', self.vm.name, 'dom0')
        self.setUpDevice(mouse_events + keyboard_events)
        self.find_device_and_start_listener(
            '{}: {}'.format(self.vm.name, 'Test input device'))
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

        self.emit_click('KEY_A')
        self.emit_click('KEY_B')
        self.emit_click('KEY_C')
        self.emit_click('KEY_D')
        for _ in xrange(4):
            self.emit_click('KEY_BACKSPACE')

        for key in ('38', '56', '54', '40'):
            self.assertEvent(['RawKeyPress', key, {}])
            self.assertEvent(['RawKeyRelease', key, {}])
        for _ in xrange(4):
            self.assertEvent(['RawKeyPress', '22', {}])
            self.assertEvent(['RawKeyRelease', '22', {}])

    def test_050_mouse_late_attach(self):
        """Test reattach at user login/GUI start - #1930"""
        self.setUpDevice(mouse_events)
        time.sleep(2)
        self.qrexec_policy('qubes.InputMouse', self.vm.name, 'dom0')
        # trigger GUI startup
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


def list_tests():
    return (
        TC_00_InputProxy,
    )
