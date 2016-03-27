RPM_SPEC_FILES.dom0 := $(if $(filter $(DIST_DOM0), $(DISTS_VM)),, rpm_spec/input-proxy.spec)
RPM_SPEC_FILES.vm := rpm_spec/input-proxy.spec
RPM_SPEC_FILES := $(RPM_SPEC_FILES.$(PACKAGE_SET))
