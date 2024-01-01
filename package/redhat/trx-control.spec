Summary: trx-control
Group: hb9ssb/trx-control
Name: trx-control
Version: %{version}
Release: %{release}
Vendor: micro systems <https://msys.ch>
Packager: https://msys.ch
License: MIT
Source: trx-control-%{version}.tar.gz
Prefix: /usr
BuildRequires: curl-devel expat-devel gcc make motif-devel
BuildRequires: postgresql%{pg_version}-devel readline-devel sqlite-devel

Requires: epel-release expat libcurl motif postgresql16-libs readline
Requires: sqlite-libs

Provides: trx-control

Buildroot: /tmp/trx-control

%description
trx-control

%files
/etc/X11/app-defaults/XQRG
/usr/bin/trxctl
/usr/bin/xqrg
/usr/lib/udev/rules.d/70-bmcm-usb-pio.rules
/usr/lib/udev/rules.d/70-ft-710.rules
/usr/lib/udev/rules.d/70-ic-705.rules
/usr/lib/udev/rules.d/70-yaesu-cat.rules
/usr/sbin/trxd
/usr/share/man/man1/trxctl.1.gz
/usr/share/man/man1/xqrg.1.gz
/usr/share/man/man7/trx-control.7.gz
/usr/share/man/man8/trxd.8.gz
/usr/share/trxctl/trxctl.lua
/usr/share/trxd/extension/logbook.lua
/usr/share/trxd/extension/ping.lua
/usr/share/trxd/extension/qrz.lua
/usr/share/trxd/gpio-controller.lua
/usr/share/trxd/gpio/bmcm-usb-pio.lua
/usr/share/trxd/lua/curl.so
/usr/share/trxd/lua/expat.so
/usr/share/trxd/lua/net.so
/usr/share/trxd/lua/pgsql.so
/usr/share/trxd/lua/sqlite.so
/usr/share/trxd/protocol/cat-5-byte.lua
/usr/share/trxd/protocol/cat-delimited.lua
/usr/share/trxd/protocol/ci-v.lua
/usr/share/trxd/protocol/simulated.lua
/usr/share/trxd/trx-controller.lua
/usr/share/trxd/trx/icom-ic-705.lua
/usr/share/trxd/trx/simulator.lua
/usr/share/trxd/trx/yaesu-ft-710.lua
/usr/share/trxd/trx/yaesu-ft-817.lua
/usr/share/trxd/trx/yaesu-ft-891.lua
/usr/share/trxd/trx/yaesu-ft-897.lua
/usr/share/trxd/trx/yaesu-ft-991a.lua
/usr/share/trxd/trxd.yaml
/usr/share/xqrg/xqrg.lua

%global debug_package %{nil}
%prep
%setup -q

%build
%{__make} %{_smp_mflags}

%install
DESTDIR=$RPM_BUILD_ROOT %{__make} %{_smp_mflags} install

%clean
rm -rf $RPM_BUILD_ROOT
%{__make} clean

%changelog
* Mon Jan 1 2024 Marc Balmer HB9SSB <info@hb9ssb.ch>
- Initial version