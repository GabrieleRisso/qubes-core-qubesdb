RPMS_DIR = rpm/
VERSION = $(file <version)

help:
	@echo "make all                   -- compile all binaries"
	@echo "make rpms-vm               -- generate binary rpm packages for VM"
	@echo "make rpms-dom0               -- generate binary rpm packages for Dom0"

rpms-dom0:
	PACKAGE_SET=dom0 rpmbuild --define "_rpmdir $(RPMS_DIR)" -bb rpm_spec/qubes-db.spec

rpms-vm:
	PACKAGE_SET=vm rpmbuild --define "_rpmdir $(RPMS_DIR)" -bb rpm_spec/qubes-db.spec

VCHAN_PKG = $(if $(BACKEND_VMM),vchan-$(BACKEND_VMM),vchan)
BACKEND_VMM ?= $(shell pkg-config --variable=backend_vmm $(VCHAN_PKG) 2>/dev/null || echo xen)

all:
	$(MAKE) -C daemon BACKEND_VMM=$(BACKEND_VMM)
	$(MAKE) -C client BACKEND_VMM=$(BACKEND_VMM)
	$(MAKE) -C python
ifneq ($(filter kvm socket,$(BACKEND_VMM)),)
	$(MAKE) -C daemon/kvm
endif

clean:
	$(MAKE) -C daemon clean
	$(MAKE) -C client clean
	$(MAKE) -C python clean
	$(MAKE) -C daemon/kvm clean

install:
	$(MAKE) -C daemon install
	$(MAKE) -C client install
	$(MAKE) -C python install
	$(MAKE) -C include install
ifneq ($(filter kvm socket,$(BACKEND_VMM)),)
	$(MAKE) -C daemon/kvm install
endif

msi:
	$(MAKE) -C python install PYTHON_PREFIX_ARG=--prefix=. DESTDIR=python3
	candle -arch x64 -dversion=$(VERSION) installer.wxs
	light -o core-qubesdb.msm installer.wixobj
