%define __provides_exclude_from ^(%{_datadir}/%{name}/lib/|%{_datadir}/%{name}/ru/auroraos/amberpdf/).*$
%define __requires_exclude ^lib(jpeg|turbojpeg|pdfium|amberpdf|amberpdfqmlplugin)\\.so.*$

Name:       ru.auroraos.OverleafClient
Summary:    Native Overleaf client for Aurora OS
Version:    1.3.2
Release:    1
Group:      Qt/Qt
License:    Proprietary and BSD-3-Clause and IJG
URL:        https://www.overleaf.com/
Source0:    %{name}-%{version}.tar.bz2

Requires:       sailfishsilica-qt5 >= 0.10.9
Requires:       nemo-qml-plugin-notifications-qt5
BuildRequires:  pkgconfig(auroraapp)
BuildRequires:  pkgconfig(aurorawebview)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  qt5-qttools-linguist

%description
An independently developed native Overleaf client for Aurora OS.

%prep
%autosetup

%build
%cmake -GNinja
%ninja_build

%install
%ninja_install

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%defattr(644,root,root,-)
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
