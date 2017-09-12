Name:           org.kernel.kmod
Version:        1
Release:        1%{?dist}
Summary:        Kernel Module Interface
License:        ASL2.0
URL:            https://github.com/varlink/%{name}
Source0:        https://github.com/varlink/%{name}/archive/%{name}-%{version}.tar.gz
BuildRequires:  meson
BuildRequires:  gcc
BuildRequires:  pkgconfig
BuildRequires:  libvarlink-devel
BuildRequires:  kmod-devel

%description
Service to query and load kernel modules.

%prep
%setup -q

%build
%meson
%meson_build

%check
export LC_CTYPE=C.utf8
%meson_test

%install
%meson_install

%files
%license AUTHORS
%license COPYRIGHT
%license LICENSE
%{_bindir}/org.kernel.kmod

%changelog
* Tue Aug 29 2017 <info@varlink.org> 1-1
- org.kernel.kmod 1
