%define version %(cat version)
%if 0%{?qubes_builder}
%define _builddir %(pwd)
%endif
    
Name:		input-proxy
Version:	%{version}
Release:	1%{?dist}
Summary:	Simple input device proxy

Group:		System Environment/Daemons
License:	GPLv2
URL:		https://www.qubes-os.org/

BuildRequires:	kernel-headers

%description
Simple input device proxy, which pass events from /dev/input/eventN device, to
/dev/uintput. It uses stdin/stdout, so it suitable for use as Qubes RPC service.

%prep
# we operate on the current directory, so no need to unpack anything
# symlink is to generate useful debuginfo packages
rm -f %{name}-%{version}
ln -sf . %{name}-%{version}
%setup -T -D

%build
make %{?_smp_mflags} all


%install
make install DESTDIR=%{buildroot}


%files
%doc README.md
%defattr(-,root,root,-)
/usr/bin/input-proxy-sender
/usr/bin/input-proxy-receiver


%changelog

