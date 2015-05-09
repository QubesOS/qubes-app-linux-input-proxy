#include <unistd.h>

int read_all(int fd, void *buf, size_t size) {
    size_t pos = 0;
    int ret;
    
    while (pos < size) {
        ret = read(fd, buf+pos, size-pos);
        if (ret <= 0)
            return ret;
        pos += ret;
    }
    return pos;
}

int write_all(int fd, void *buf, size_t size) {
    size_t pos = 0;
    int ret;
    
    while (pos < size) {
        ret = write(fd, buf+pos, size-pos);
        if (ret == -1)
            return -1;
        pos += ret;
    }
    return pos;
}
