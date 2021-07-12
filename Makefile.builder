ifneq ($(filter $(DISTRIBUTION), debian qubuntu),)
    DEBIAN_BUILD_DIRS := debian
endif

RPM_SPEC_FILES := rpm_spec/input-proxy.spec

ARCH_BUILD_DIRS := archlinux
