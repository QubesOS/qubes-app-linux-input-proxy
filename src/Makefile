CFLAGS += -g -Wall -Wextra -Werror -O2 -std=gnu99

# See GCC bug #53229 and #64709
CFLAGS += -Wno-missing-braces -Wno-missing-field-initializers

all: input-proxy-sender input-proxy-receiver

input-proxy-sender: input-proxy-sender.o common.o
input-proxy-receiver: input-proxy-receiver.o common.o

clean:
	rm -f *.o input-proxy-sender input-proxy-receiver

install:
	install -d $(DESTDIR)/usr/bin
	install input-proxy-sender $(DESTDIR)/usr/bin/input-proxy-sender
	install input-proxy-receiver $(DESTDIR)/usr/bin/input-proxy-receiver
