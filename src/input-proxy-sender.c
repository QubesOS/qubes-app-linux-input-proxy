#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <errno.h>
#include <linux/input.h>
#include "protocol.h"
#include "common.h"

int send_caps(int fd) {
    struct input_proxy_device_caps_msg caps_msg = { 0 };
    struct input_proxy_device_caps caps = { 0 };
    struct input_proxy_hello hello = {
        .version = INPUT_PROXY_PROTOCOL_VERSION,
        .caps_size = sizeof caps_msg
    };
    int rc = 0;
    int i;

    if (rc != -1) rc = ioctl(fd, EVIOCGPROP(       sizeof caps.propbit), caps.propbit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(0,      sizeof caps.evbit),    caps.evbit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof caps.keybit),   caps.keybit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(EV_REL, sizeof caps.relbit),   caps.relbit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof caps.absbit),   caps.absbit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(EV_MSC, sizeof caps.mscbit),   caps.mscbit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(EV_LED, sizeof caps.ledbit),   caps.ledbit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(EV_SND, sizeof caps.sndbit),   caps.sndbit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(EV_FF,  sizeof caps.ffbit),    caps.ffbit);
    if (rc != -1) rc = ioctl(fd, EVIOCGBIT(EV_SW , sizeof caps.swbit),    caps.swbit);
    if (rc == -1) {
        perror("ioctl get caps");
        return -1;
    }
    caps_msg.caps = caps;

    /* get name */
    rc = ioctl(fd, EVIOCGNAME(sizeof caps_msg.name), caps_msg.name);
    if (rc == -1) {
        /* ignore the case of no name */
        if (errno != ENOENT) {
            perror("ioctl get name");
            return -1;
        }
    } else {
        for (i = 0; i < rc; i++) {
            if (!caps_msg.name[i]) {
                /* zero-out unused space */
                memset(caps_msg.name+i, 0, sizeof(caps_msg.name)-i);
                break;
            }
            /* replace non-ASCII chars with '_' */
            if (caps_msg.name[i] < 0x20 || caps_msg.name[i] >= 0x7f)
                caps_msg.name[i] = '_';
        }
        caps_msg.name[sizeof(caps_msg.name)-1] = 0;
    }

    for (i = 0; i < ABS_CNT; i++) {
        if (caps.absbit[i / BITS_PER_LONG] & (1UL<<(i & (BITS_PER_LONG-1)))) {
            rc = ioctl(fd, EVIOCGABS(i), &caps_msg.absinfo[i]);
            if (rc == -1) {
                perror("ioctl get absinfo");
                return -1;
            }
        }
    }

    rc = write_all(1, &hello, sizeof(hello));
    if (rc == -1)
        return rc;
    rc = write_all(1, &caps_msg, sizeof(caps_msg));
    return rc;
}

int ignore_led_events(int fd) {
    int rc;
    unsigned long bits[BITS_TO_LONGS(LED_CNT)];
    struct input_mask input_mask = {
        .type       = EV_LED,
        .codes_size = sizeof(bits),
        .codes_ptr  = (uintptr_t)bits,
    };

    memset(bits, 0, sizeof(bits));

    rc = ioctl(fd, EVIOCSMASK, &input_mask);
    if (rc == -1) {
        perror("ioctl EVIOCSMASK");
        return -1;
    }

    return 0;
}

int pass_event(int src_fd, int dst_fd) {
    int rc;
    struct input_event ev;

    rc = read_all(src_fd, &ev, sizeof(ev));
    if (rc == 0)
        return 0;
    /* treat device disconnect as EOF */
    if (rc == -1 && errno == ENODEV)
        return 0;
    if (rc == -1) {
        perror("read");
        return -1;
    }
    rc = write_all(dst_fd, &ev, sizeof(ev));
    if (rc == -1) {
        perror("write");
        return -1;
    }
    return rc;
}

int process_events(int fd) {
    struct pollfd fds[] = {
        { .fd = 0,  .events = POLLIN, },
        { .fd = fd, .events = POLLIN, }
    };
    int rc = 0;

    while ((rc=poll(fds, 2, -1)) > 0) {
        if (fds[0].revents) {
            rc = pass_event(0, fd);
            if (rc <= 0)
                return rc;
        }
        if (fds[1].revents) {
            rc = pass_event(fd, 1);
            if (rc <= 0)
                return rc;
        }
    }
    if (rc == -1) {
        perror("poll");
        return -1;
    }
    return 0;
}

void usage() {
    fprintf(stderr, "Usage: input-proxy-sender <device-file-path>\n");
}

int main(int argc, char **argv) {
    int fd;

    if (argc < 2) {
        usage();
        return 1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    /* grab the device so other clients will not receive forwarded events; this
     * is to prevent:
     * 1. receiving the events by USB VM windows when not really active (X
     *    server in USB VM will also have this device connected)
     * 2. receiving the events twice when USB VM window is active: one from
     *    real device and the second from gui-agent
     */
    if (ioctl(fd, EVIOCGRAB, 1) == -1) {
        perror("ioctl grab");
        return 1;
    }

    if (send_caps(fd) == -1)
        return 1;

    /* TODO: produce synthetic EV_REP initial events for keyboard */

    /* The input proxy sender is a client of the evdev driver and has an active
     * grab. By default, Linux will send the LED events back to us, even if we
     * sent them. This causes the events to recirculate forever, consuming lots
     * of CPU and rendering the input device unusable. Avoid this issue by
     * requesting Linux to not send LED events to us.
     */
    if (ignore_led_events(fd) == -1)
        return 1;

    if (process_events(fd) == -1)
        return 1;

    if (ioctl(fd, EVIOCGRAB, 0) == -1) {
        if (errno != ENODEV) {
            perror("ioctl grab");
            return 1;
        }
    }

    close(fd);
    return 0;
}



