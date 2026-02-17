CFLAGS += -I../../include -I.. -g -Wall -Werror -pie -fPIC -O2
BINDIR ?= /usr/bin

all: qubesdb-config-inject qubesdb-config-read

qubesdb-config-inject: qubesdb-config-inject.o
	$(CC) $(LDFLAGS) -o $@ $^

qubesdb-config-read: qubesdb-config-read.o
	$(CC) $(LDFLAGS) -o $@ $^

install:
	install -d $(DESTDIR)$(BINDIR)
	install qubesdb-config-inject $(DESTDIR)$(BINDIR)/
	install qubesdb-config-read $(DESTDIR)$(BINDIR)/

clean:
	rm -f *.o qubesdb-config-inject qubesdb-config-read
