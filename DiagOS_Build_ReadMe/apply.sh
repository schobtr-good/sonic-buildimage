#!/bin/bash
patch -p0 ../functions.sh diff/functions.sh.patch
patch -p0 ../build_image.sh diff/build_image.sh.patch
patch -p0 ../build_debian.sh diff/build_debian.sh.patch
patch -p0 ../sonic-slave-stretch/Dockerfile.j2 diff/Dockerfile.j2.patch
patch -p0 ../onie-image.conf diff/onie-image.conf.patch
patch -p0 ../src/sonic-linux-kernel/Makefile diff/sonic-linux-kernel_Makefile.patch
patch -p0 ../src/sonic-utilities/sonic_installer/main.py diff/sonic-utilities_sonic_installer_main.py.patch
patch -p0 ../src/sonic-utilities/scripts/reboot diff/sonic-utilities_scripts_reboot.patch
