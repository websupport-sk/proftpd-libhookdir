prefix := /usr/lib/proftpd

all:
	gcc libhookdir.c -g -shared -fPIC -o libhookdir.so

clean:
	-rm libhookdir.so

install:
	install -m 0755 libhookdir.so ${prefix}/libhookdir.so
