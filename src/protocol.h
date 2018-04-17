#include <linux/input.h>
#include <stdint.h>

#define INPUT_PROXY_PROTOCOL_VERSION 1

struct input_proxy_hello {
    uint32_t version;
    uint32_t caps_size;
};


#define BITS_PER_BYTE 8
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define BITS_PER_LONG (BITS_PER_BYTE * sizeof(long))
#define BITS_TO_LONGS(nr)     DIV_ROUND_UP(nr, BITS_PER_LONG)

struct input_proxy_device_caps {
    unsigned long propbit[BITS_TO_LONGS(INPUT_PROP_CNT)];
    unsigned long evbit[BITS_TO_LONGS(EV_CNT)];
    unsigned long keybit[BITS_TO_LONGS(KEY_CNT)];
    unsigned long relbit[BITS_TO_LONGS(REL_CNT)];
    unsigned long absbit[BITS_TO_LONGS(ABS_CNT)];
    unsigned long mscbit[BITS_TO_LONGS(MSC_CNT)];
    unsigned long ledbit[BITS_TO_LONGS(LED_CNT)];
    unsigned long sndbit[BITS_TO_LONGS(SND_CNT)];
    unsigned long ffbit[BITS_TO_LONGS(FF_CNT)];
    unsigned long swbit[BITS_TO_LONGS(SW_CNT)];
};

/* linux/input/h:
 *
 * struct input_absinfo {
 *     __s32 value;
 *     __s32 minimum;
 *     __s32 maximum;
 *     __s32 fuzz;
 *     __s32 flat;
 *     __s32 resolution;
 * };
*/

struct input_proxy_device_caps_msg {
    struct input_proxy_device_caps caps;
    /* from this point, structure may be truncated after any field (and
     * input_proxy_hello.caps_size set smaller accordingly); default value of 0
     * is assumed then */
    char name[128];
    struct input_absinfo absinfo[ABS_CNT];
};
