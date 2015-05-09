#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <linux/input.h>
#include "protocol.h"
#include "common.h"

int send_caps(int fd) {
    struct input_proxy_device_caps caps = { 0 };
    struct input_proxy_hello hello = {
        .version = INPUT_PROXY_PROTOCOL_VERSION,
        .caps_size = sizeof caps
    };
    int rc = 0;

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
    rc = write_all(1, &hello, sizeof(hello));
    if (rc == -1)
        return rc;
    rc = write_all(1, &caps, sizeof(caps));
    return rc;
}

int pass_event(int src_fd, int dst_fd) {
    int rc;
    struct input_event ev;

    rc = read_all(src_fd, &ev, sizeof(ev));
    if (rc == 0)
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

    if (process_events(fd) == -1)
        return 1;

    if (ioctl(fd, EVIOCGRAB, 0) == -1) {
        perror("ioctl grab");
        return 1;
    }

    close(fd);
    return 0;
}



