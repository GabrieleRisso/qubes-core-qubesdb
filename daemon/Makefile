
CFLAGS += -I../include -I. -g -Wall -Werror -pie
EXEEXT =

SYSTEMD ?= 1

VCHAN_PKG = $(if $(BACKEND_VMM),vchan-$(BACKEND_VMM),vchan)
BACKEND_VMM ?= $(shell pkg-config --variable=backend_vmm $(VCHAN_PKG) 2>/dev/null || echo xen)

ifneq ($(OS),Windows_NT)
CFLAGS += -fPIC -O2
CFLAGS += $(shell pkg-config --cflags $(VCHAN_PKG))
ifeq ($(SYSTEMD),1)
CFLAGS += `pkg-config --cflags libsystemd || pkg-config --cflags libsystemd-daemon`
CFLAGS += -DHAVE_SYSTEMD
LIBS += `pkg-config --libs libsystemd || pkg-config --libs libsystemd-daemon`
endif
LIBS += $(shell pkg-config --libs $(VCHAN_PKG))

else # Windows_NT
EXEEXT = .exe
CFLAGS += -I$(QUBES_INCLUDES)
CFLAGS += -DUNICODE -D_UNICODE
LIBS += -L$(QUBES_LIBS) -lvchan
endif
CFLAGS += -DBACKEND_VMM_$(BACKEND_VMM)

BINDIR ?= /usr/bin

all: qubesdb-daemon$(EXEEXT)

install:
	install -d $(DESTDIR)$(BINDIR)
	install qubesdb-daemon$(EXEEXT) $(DESTDIR)$(BINDIR)/

qubesdb-daemon$(EXEEXT): db-cmds.o db-daemon.o db-core.o buffer.o
	$(CC) $(LDFLAGS) -o $@ $^ $(APPEND_LDFLAGS) $(LIBS)

clean:
	rm -f *.o qubesdb-daemon$(EXEEXT)
