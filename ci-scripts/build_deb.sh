#!/bin/bash

set -e

echo "creating deb for ${PROGRAMNAME}"
mkdir -p ./debian/DEBIAN
mkdir -p ./debian/usr/bin
find ./debian -type d | xargs chmod 755
cp -v "${PROGRAMNAME}/${PROGRAMNAME}" ./debian/usr/bin/

# round((size in bytes)/1024)
INSTALLED_SIZE=$(du -s ./debian/usr | awk '{x=$1/1024; i=int(x); if ((x-i)*10 >= 5) {f=1} else {f=0}; print i+f}')
echo "size=${INSTALLED_SIZE}"

echo "Package: ${PROGRAMNAME}
Version: ${VERSION}
Section: contrib
Priority: optional
Architecture: amd64
Depends: libqt5widgets5 (>= 5.0.0), libimobiledevice-utils (>= 1.1.5), usbmuxd (>= 1.0.8), android-tools-adb (>= 4.2.2)
Installed-Size: ${INSTALLED_SIZE}
Maintainer: Alexander Lopatin <alopatindev.spamhere@gmail.com>
Description: Crossplatform Android, iOS and text file log viewer
 This programs shows a log from devices that run Android and iOS.
 Also it is a GUI for tailf command-line tool." > debian/DEBIAN/control

fakeroot dpkg-deb --build debian
mv debian.deb "${OUTPUT_ARCHIVE_NAME}.deb"

# vim: textwidth=0
