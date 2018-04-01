#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include "protocol.h"
#include "common.h"

#define UINPUT_DEVICE "/dev/uinput"

/* compile workaround for older system */
#ifndef SYN_MAX
#define SYN_MAX 0xf
#endif

/* do not create device, send events to stdout, also do not read from stdin
 * - useful for testing */
// #define FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION

struct options {
    char *name;
    int vendor;
    int product;
    struct input_proxy_device_caps caps;
    struct input_absinfo absinfo[ABS_CNT];
};

void long_and(unsigned long *dst, unsigned long *src, size_t longs_count) {
    size_t i;

    for (i = 0; i < longs_count; i++)
        dst[i] &= src[i];
}
#define LONG_AND(dst, src) long_and(dst, src, sizeof(dst)/sizeof(dst[0]))

int long_test_bit(unsigned long *bitfield, int bit, size_t bitfield_size) {
    if (bit / BITS_PER_LONG >= bitfield_size)
        return 0;
    return (bitfield[bit / BITS_PER_LONG] &
                    (1UL<<(bit & (BITS_PER_LONG-1)))) != 0;
}
#define LONG_TEST_BIT(bitfield, bit) long_test_bit(bitfield, bit, \
        sizeof(bitfield)/sizeof(bitfield[0]))

void long_set_bit(unsigned long *bitfield, int bit, size_t bitfield_size) {
    if (bit / BITS_PER_LONG >= bitfield_size)
        return;

    bitfield[bit / BITS_PER_LONG] |= (1UL<<(bit & (BITS_PER_LONG-1)));
}
#define LONG_SET_BIT(bitfield, bit) long_set_bit(bitfield, bit, \
        sizeof(bitfield)/sizeof(bitfield[0]))


/* receive hello and device caps, then process according to opt->caps - allow
 * only those set there, then adjust opt->caps to have only really supported
 * capabilities by remote end
 *
 * returns: -1 on error, 0 on EOF, 1 on success
 */
int receive_and_validate_caps(struct options *opt) {
    struct input_proxy_device_caps_msg untrusted_caps_msg;
    struct input_proxy_hello untrusted_hello;
    size_t caps_size, i;
    int rc;

    rc = read_all(0, &untrusted_hello, sizeof(untrusted_hello));
    if (rc == 0)
        return 0;
    if (rc == -1) {
        perror("read hello");
        return -1;
    }

    if (untrusted_hello.version != INPUT_PROXY_PROTOCOL_VERSION) {
        fprintf(stderr, "Incompatible remote protocol version: %d\n",
                untrusted_hello.version);
        return -1;
    }

    caps_size = untrusted_hello.caps_size;
    if (caps_size > sizeof(untrusted_caps_msg))
        caps_size = sizeof(untrusted_caps_msg);
    memset(&untrusted_caps_msg, 0, sizeof(untrusted_caps_msg));

    rc = read_all(0, &untrusted_caps_msg, caps_size);
    if (rc == 0)
        return 0;
    if (rc == -1) {
        perror("read caps");
        return -1;
    }
    if (untrusted_hello.caps_size > sizeof(untrusted_caps_msg)) {
        /* discard the rest (if any); this will work, because we already
         * checked protocol version - in case of not compatible protocol
         * change, the version would be different */
        size_t to_discard = untrusted_hello.caps_size - sizeof(untrusted_caps_msg);
        char discard_buffer[128];
        while (to_discard) {
            if (to_discard < sizeof(discard_buffer))
                rc = read_all(0, discard_buffer, to_discard);
            else
                rc = read_all(0, discard_buffer, sizeof(discard_buffer));
            if (rc == 0)
                return 0;
            if (rc == -1) {
                perror("discard caps");
                return -1;
            }
            to_discard -= rc;
        }
    }

    LONG_AND(opt->caps.propbit, untrusted_caps_msg.caps.propbit);
    LONG_AND(opt->caps.evbit,   untrusted_caps_msg.caps.evbit);
#define APPLY_BITS(_evflag, _field) \
    if (LONG_TEST_BIT(opt->caps.evbit, _evflag)) \
        LONG_AND(opt->caps._field, untrusted_caps_msg.caps._field); \
    else \
        memset(opt->caps._field, 0, sizeof(opt->caps._field));

    APPLY_BITS(EV_KEY, keybit);
    APPLY_BITS(EV_REL, relbit);
    APPLY_BITS(EV_ABS, absbit);
    APPLY_BITS(EV_MSC, mscbit);
    APPLY_BITS(EV_LED, ledbit);
    APPLY_BITS(EV_SND, sndbit);
    APPLY_BITS(EV_FF,  ffbit);
    APPLY_BITS(EV_SW,  swbit);
#undef APPLY_BITS

    /* use VM-provided name only if not already specified */
    if (!opt->name && untrusted_caps_msg.name[0]) {
        unsigned i;

        opt->name = calloc(sizeof(untrusted_caps_msg.name), 1);
        for (i = 0; i < sizeof(untrusted_caps_msg.name); i++) {
            if (untrusted_caps_msg.name[i] == 0)
                /* opt->name initially zero-ed, so no need to copy that \0 */
                break;
            /* allow only ASCII, excluding control characters */
            if (untrusted_caps_msg.name[i] >= 0x20 && 
                    untrusted_caps_msg.name[i] < 0x7f)
                opt->name[i] = untrusted_caps_msg.name[i];
            else {
                fprintf(stderr, "Invalid characters in device name\n");
                return -1;
            }
        }
        /* make sure the name is terminated with \0 */
        opt->name[sizeof(untrusted_caps_msg.name)-1] = 0;
    }

    /* copy input_absinfo for EV_ABS; if given info is missing, disable that
     * axis */
    for (i = 0; i < ABS_CNT; i++) {
        if (LONG_TEST_BIT(opt->caps.absbit, i)) {
            if (!untrusted_caps_msg.absinfo[i].minimum &&
                    !untrusted_caps_msg.absinfo[i].maximum) {
                /* no axis limits are provided, disable it */
                opt->caps.absbit[i / BITS_PER_LONG] &= ~(1UL<<(i & (BITS_PER_LONG-1)));
                continue;
            }
            /* here is place for some validation of axis data, if we come up
             * with any - in addition to those done by Linux kernel, and later
             * input driver */
            opt->absinfo[i].value = untrusted_caps_msg.absinfo[i].value;
            opt->absinfo[i].minimum = untrusted_caps_msg.absinfo[i].minimum;
            opt->absinfo[i].maximum = untrusted_caps_msg.absinfo[i].maximum;
            opt->absinfo[i].resolution =
                untrusted_caps_msg.absinfo[i].resolution;
            opt->absinfo[i].fuzz = untrusted_caps_msg.absinfo[i].fuzz;
            opt->absinfo[i].flat = untrusted_caps_msg.absinfo[i].flat;
        }
    }

    return 1;
}

int send_bits(int fd, int ioctl_num, unsigned long *bits, size_t bits_count) {
    size_t i;

    for (i = 0; i < bits_count; i++) {
        if (long_test_bit(bits, i, BITS_TO_LONGS(bits_count))) {
            if (ioctl(fd, ioctl_num, i) == -1) {
                perror("ioctl set bit");
                return -1;
            }
        }
    }
    return 0;
}

#if UINPUT_VERSION >= 5
int send_absinfo(int fd,
        unsigned long abs_bits[BITS_TO_LONGS(ABS_CNT)],
        struct input_absinfo *absinfo) {
    struct uinput_abs_setup absinfo_setup;
    size_t i;

    for (i = 0; i < ABS_CNT; i++) {
        if (long_test_bit(abs_bits, i, BITS_TO_LONGS(ABS_CNT))) {
            absinfo_setup.code = i;
            absinfo_setup.absinfo = absinfo[i];
            if (ioctl(fd, UI_ABS_SETUP, &absinfo_setup) == -1) {
                perror("ioctl set absinfo");
                return -1;
            }
        }
    }
    return 0;
}
#endif

int register_device(struct options *opt, int fd) {
#if UINPUT_VERSION >= 5
    struct uinput_setup uinput_setup = { 0 };
#endif
    struct uinput_user_dev uinput_dev = { 0 };
    int rc = 0;
    char *domain_name = NULL;

    if (!rc)
        rc = send_bits(fd, UI_SET_EVBIT, opt->caps.evbit, EV_CNT);
    if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_KEY)) 
        rc = send_bits(fd, UI_SET_KEYBIT, opt->caps.keybit, KEY_CNT);
    if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_REL)) 
        rc = send_bits(fd, UI_SET_RELBIT, opt->caps.relbit, REL_CNT);
    if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_ABS)) 
        rc = send_bits(fd, UI_SET_ABSBIT, opt->caps.absbit, ABS_CNT);
    if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_MSC)) 
        rc = send_bits(fd, UI_SET_MSCBIT, opt->caps.mscbit, MSC_CNT);
    if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_LED)) 
        rc = send_bits(fd, UI_SET_LEDBIT, opt->caps.ledbit, LED_CNT);
    if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_SND)) 
        rc = send_bits(fd, UI_SET_SNDBIT, opt->caps.sndbit, SND_CNT);
    if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_FF)) 
        rc = send_bits(fd, UI_SET_FFBIT, opt->caps.ffbit, FF_CNT);
    if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_SW)) 
        rc = send_bits(fd, UI_SET_SWBIT, opt->caps.swbit, SW_CNT);
    if (rc == -1) {
        close(fd);
        return -1;
    }

    if (!opt->name)
        opt->name = strdup("Forwarded input device");
    domain_name = getenv("QREXEC_REMOTE_DOMAIN");
    if (domain_name) {
        snprintf(uinput_dev.name, UINPUT_MAX_NAME_SIZE, "%s: %s",
                domain_name, opt->name);
        /* make sure string is terminated, in case it was truncated */
        uinput_dev.name[UINPUT_MAX_NAME_SIZE-1] = 0;
    } else {
        strncpy(uinput_dev.name, opt->name, UINPUT_MAX_NAME_SIZE);
    }

    uinput_dev.id.bustype = BUS_USB;
    uinput_dev.id.vendor = opt->vendor;
    uinput_dev.id.product = opt->product;
    uinput_dev.id.version = 1;

#if UINPUT_VERSION >= 5
    uinput_setup.id = uinput_dev.id;
    strncpy(uinput_setup.name, uinput_dev.name, sizeof(uinput_setup.name));

    rc = ioctl(fd, UI_DEV_SETUP, &uinput_setup);
    if (rc == -1 && errno != EINVAL) {
        perror("ioctl UI_DEV_SETUP");
        return -1;
    } else if (rc == -1 && errno == EINVAL) {
#endif
        size_t i;

        /* fallback to old uinput_user_dev method */
        if (LONG_TEST_BIT(opt->caps.evbit, EV_ABS)) {
            for (i = 0; i < ABS_CNT; i++) {
                if (LONG_TEST_BIT(opt->caps.absbit, i)) {
                    uinput_dev.absmax[i] = opt->absinfo[i].maximum;
                    uinput_dev.absmin[i] = opt->absinfo[i].minimum;
                    uinput_dev.absfuzz[i] = opt->absinfo[i].fuzz;
                    uinput_dev.absflat[i] = opt->absinfo[i].flat;
                }
            }
        }
        if (write_all(fd, &uinput_dev, sizeof(uinput_dev)) == -1) {
            return -1;
        }
#if UINPUT_VERSION >= 5
    } else {
        /* new method worked, send absinfo using new method */
        if (!rc && LONG_TEST_BIT(opt->caps.evbit, EV_ABS))
            rc = send_absinfo(fd, opt->caps.absbit, opt->absinfo);
        if (rc == -1)
            return -1;
    }
#endif
    if (ioctl(fd, UI_DEV_CREATE) == -1) {
        perror("ioctl dev create");
        return -1;
    }
    return 0;
}

int validate_and_forward_event(struct options *opt, int src, int dst) {
    struct input_event untrusted_event;
    struct input_event ev = { 0 };
    int rc;

    rc = read_all(src, &untrusted_event, sizeof(untrusted_event));
    if (rc == 0)
        return 0;
    if (rc == -1) {
        perror("read event");
        return -1;
    }
    /* ignore untrusted_event.time */;

    if (LONG_TEST_BIT(opt->caps.evbit, untrusted_event.type) == 0)
        return 1; /* ignore unsupported/disabled events */
    ev.type = untrusted_event.type;
    switch (ev.type) {
        case EV_SYN:
            if (untrusted_event.code > SYN_MAX)
                return -1;
            ev.code = untrusted_event.code;
            ev.value = 0;
            break;
        case EV_KEY:
            if (LONG_TEST_BIT(opt->caps.keybit, untrusted_event.code) == 0)
                return 1; /* ignore unsupported/disabled key */
            ev.code = untrusted_event.code;
            /* XXX values: 0: release, 1: press, 2: repeat */
            ev.value = untrusted_event.value;
            break;
        case EV_REL:
            if (LONG_TEST_BIT(opt->caps.relbit, untrusted_event.code) == 0)
                return 1; /* ignore unsupported/disabled axis */
            ev.code = untrusted_event.code;
            ev.value = untrusted_event.value;
            break;
        case EV_ABS:
            if (LONG_TEST_BIT(opt->caps.absbit, untrusted_event.code) == 0)
                return 1; /* ignore unsupported/disabled axis */
            ev.code = untrusted_event.code;
            ev.value = untrusted_event.value;
            break;
        case EV_MSC:
            if (LONG_TEST_BIT(opt->caps.mscbit, untrusted_event.code) == 0)
                return 1; /* ignore unsupported/disabled */
            ev.code = untrusted_event.code;
            ev.value = untrusted_event.value;
            break;
        case EV_SW:
            if (LONG_TEST_BIT(opt->caps.swbit, untrusted_event.code) == 0)
                return 1; /* ignore unsupported/disabled */
            ev.code = untrusted_event.code;
            ev.value = untrusted_event.value;
            break;
        case EV_LED:
            if (LONG_TEST_BIT(opt->caps.ledbit, untrusted_event.code) == 0)
                return 1; /* ignore unsupported/disabled */
            ev.code = untrusted_event.code;
            ev.value = untrusted_event.value;
            break;
        case EV_SND:
            if (LONG_TEST_BIT(opt->caps.sndbit, untrusted_event.code) == 0)
                return 1; /* ignore unsupported/disabled */
            ev.code = untrusted_event.code;
            ev.value = untrusted_event.value;
            break;
        case EV_REP:
        case EV_FF:
        case EV_PWR:
        default:
            fprintf(stderr, "Unsupported event type %d\n", ev.type);
            return -1;
    }

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    rc = write_all(dst, &ev, sizeof(ev));
    if (rc == -1)
        perror("write event");
#else
    rc = sizeof(ev);
#endif
    return rc;
}

int process_events(struct options *opt, int fd) {
    struct pollfd fds[] = {
        { .fd = 0,  .events = POLLIN, .revents = 0, },
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        { .fd = fd, .events = POLLIN, .revents = 0, }
#endif
    };
    int rc = 0;

    while ((rc=poll(fds, sizeof(fds)/sizeof(fds[0]), -1)) > 0) {
        if (fds[0].revents) {
            rc = validate_and_forward_event(opt, 0, fd);
            if (rc <= 0)
                return rc;
        }
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        if (fds[1].revents) {
            rc = validate_and_forward_event(opt, fd, 1);
            if (rc <= 0)
                return rc;
        }
#endif
    }
    if (rc == -1) {
        perror("poll");
        return -1;
    }
    return 0;
}

void usage() {
    fprintf(stderr, "Usage: input-proxy-receiver [options...]\n");
    fprintf(stderr, "       --mouse, -m - allow remote device act as mouse\n");
    fprintf(stderr, "    --keyboard, -k - allow remote device act as keyboard\n");
    fprintf(stderr, "      --tablet, -t - allow remote device act as tablet\n");
    fprintf(stderr, "   --name=NAME, -n - set device name\n");
    fprintf(stderr, "   --vendor=ID,    - set device vendor ID (hex)\n");
    fprintf(stderr, "  --product=ID,    - set device product ID (hex)\n");
}

#define OPT_VENDOR  128
#define OPT_PRODUCT 129

int parse_options(struct options *opt, int argc, char **argv) {
    struct option opts[] = {
        { "mouse",     0, 0, 'm' },
        { "keyboard",  0, 0, 'k' },
        { "tablet",    0, 0, 't' },
        { "name",      1, 0, 'n' },
        { "vendor",    1, 0, OPT_VENDOR },
        { "product",   1, 0, OPT_PRODUCT },
        { 0 }
    };
    int o;

    memset(opt, 0, sizeof(*opt));
    opt->name = NULL;
    opt->vendor = 0xffff;
    opt->product = 0xffff;
    LONG_SET_BIT(opt->caps.evbit, EV_SYN);

    while ((o = getopt_long(argc, argv, "mktn:v:p:", opts, NULL)) != -1) {
        switch (o) {
            case 'm':
                LONG_SET_BIT(opt->caps.evbit, EV_REL);
                LONG_SET_BIT(opt->caps.evbit, EV_KEY);
                /* TODO: some configuration for that */
                memset(opt->caps.relbit, 0xff, sizeof(opt->caps.relbit));
                LONG_SET_BIT(opt->caps.keybit, BTN_LEFT);
                LONG_SET_BIT(opt->caps.keybit, BTN_RIGHT);
                LONG_SET_BIT(opt->caps.keybit, BTN_MIDDLE);
                LONG_SET_BIT(opt->caps.keybit, BTN_SIDE);
                LONG_SET_BIT(opt->caps.keybit, BTN_EXTRA);
                LONG_SET_BIT(opt->caps.keybit, BTN_FORWARD);
                LONG_SET_BIT(opt->caps.keybit, BTN_BACK);
                LONG_SET_BIT(opt->caps.keybit, BTN_TASK);
                break;
            case 'k':
                LONG_SET_BIT(opt->caps.evbit, EV_KEY);
                LONG_SET_BIT(opt->caps.evbit, EV_LED);
                /* TODO: some configuration for that */
                memset(opt->caps.keybit, 0xff, sizeof(opt->caps.keybit));
                memset(opt->caps.ledbit, 0xff, sizeof(opt->caps.ledbit));
                break;
            case 't':
                LONG_SET_BIT(opt->caps.evbit, EV_ABS);
                /* TODO: some configuration for that */
                memset(opt->caps.absbit, 0xff, sizeof(opt->caps.absbit));
                LONG_SET_BIT(opt->caps.evbit, EV_KEY);
                LONG_SET_BIT(opt->caps.keybit, BTN_DIGI);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOUCH);
                LONG_SET_BIT(opt->caps.keybit, BTN_LEFT);
                LONG_SET_BIT(opt->caps.keybit, BTN_RIGHT);
                LONG_SET_BIT(opt->caps.keybit, BTN_MIDDLE);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_PEN);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_RUBBER);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_BRUSH);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_PENCIL);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_AIRBRUSH);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_FINGER);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_MOUSE);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_LENS);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_DOUBLETAP);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_TRIPLETAP);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_QUADTAP);
                LONG_SET_BIT(opt->caps.keybit, BTN_TOOL_QUINTTAP);
                LONG_SET_BIT(opt->caps.keybit, BTN_STYLUS);
                LONG_SET_BIT(opt->caps.keybit, BTN_STYLUS2);
                /* not available with older headers */
#               ifdef BTN_STYLUS3
                LONG_SET_BIT(opt->caps.keybit, BTN_STYLUS3);
#               endif
                break;
            case 'n':
                opt->name = optarg;
                break;
            case OPT_VENDOR:
                opt->vendor = strtoul(optarg, NULL, 16);
                break;
            case OPT_PRODUCT:
                opt->product = strtoul(optarg, NULL, 16);
                break;
            default:
                usage();
                return -1;
        }
    }

    return 0;
}

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
int input_proxy_receiver_main(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
    struct options opt = { 0 };
    int fd = -1;
    int rc;

    rc = parse_options(&opt, argc, argv);
    if (rc == -1)
        return 1;

    rc = receive_and_validate_caps(&opt);
    if (rc <= 0) {
        rc = (rc == -1);
        goto out;
    }

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    fd = 1;
#else
    fd = open(UINPUT_DEVICE, O_RDWR);
    if (fd == -1) {
        perror("open " UINPUT_DEVICE);
        rc = 1;
        goto out;
    }

    rc = register_device(&opt, fd);
    if (rc == -1) {
        rc = 1;
        goto out;
    }
#endif

    rc = process_events(&opt, fd);
    if (rc == -1) {
        rc = 1;
        goto out;
    }

    rc = 0;
out:
    if (fd != -1)
        close(fd);
    if (opt.name)
        free(opt.name);
    return rc;
}
