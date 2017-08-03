%define build_date %(date +"%%a %%b %%d %%Y")
%define build_timestamp %(date +"%%Y%%m%%d.%%H%m%%S")

Name:           org.kernel.kmod
Version:        1
Release:        %{build_timestamp}%{?dist}
Summary:        Kernel Module Interface
License:        ASL2.0
URL:            https://github.com/varlink/org.kernel.kmod
Source0:        https://github.com/varlink/org.kernel.kmod/archive/v%{version}.tar.gz
BuildRequires:  autoconf automake pkgconfig
BuildRequires:  libvarlink-devel
BuildRequires:  kmod-devel

%description
Service to query and load kernel modules.

%prep
%setup -q

%build
./autogen.sh
%configure
make %{?_smp_mflags}

%install
%make_install

%files
%license AUTHORS
%license COPYRIGHT
%license LICENSE
%{_bindir}/org.kernel.kmod

%changelog
* %{build_date} <info@varlink.org> %{version}-%{build_timestamp}
- %{name} %{version}
