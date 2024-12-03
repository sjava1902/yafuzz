#!/usr/bin/env bash
# Copyright 2016 syzkaller project authors. All rights reserved.
# Use of this source code is governed by Apache 2 LICENSE that can be found in the LICENSE file.

# create-image.sh creates a minimal Debian Linux image suitable for syzkaller.

set -eux

# Create a minimal Debian distribution in a directory.
PREINSTALL_PKGS=openssh-server,curl,tar,gcc,libc6-dev,time,strace,sudo,less,psmisc,selinux-utils,policycoreutils,checkpolicy,selinux-policy-default,firmware-atheros,debian-ports-archive-keyring,ocfs2-tools,drbd-utils,netcat,telnet

# If ADD_PACKAGE is not defined as an external environment variable, use our default packages
if [ -z ${ADD_PACKAGE+x} ]; then
    ADD_PACKAGE="make,sysbench,git,vim,tmux,usbutils,tcpdump"
fi

# Variables affected by options
ARCH=$(uname -m)
RELEASE=bullseye
FEATURE=minimal
SEEK=2047
PERF=false

# Display help function
display_help() {
    echo "Usage: $0 [option...] " >&2
    echo
    echo "   -a, --arch                 Set architecture"
    echo "   -d, --distribution         Set on which debian distribution to create"
    echo "   -f, --feature              Check what packages to install in the image, options are minimal, full"
    echo "   -s, --seek                 Image size (MB), default 2048 (2G)"
    echo "   -h, --help                 Display help message"
    echo "   -p, --add-perf             Add perf support with this option enabled. Please set envrionment variable \$KERNEL at first"
    echo
}

while true; do
    if [ $# -eq 0 ];then
	echo $#
	break
    fi
    case "$1" in
        -h | --help)
            display_help
            exit 0
            ;;
        -a | --arch)
	    ARCH=$2
            shift 2
            ;;
        -d | --distribution)
	    RELEASE=$2
            shift 2
            ;;
        -f | --feature)
	    FEATURE=$2
            shift 2
            ;;
        -s | --seek)
	    SEEK=$(($2 - 1))
            shift 2
            ;;
        -p | --add-perf)
	    PERF=true
            shift 1
            ;;
        -*)
            echo "Error: Unknown option: $1" >&2
            exit 1
            ;;
        *)  # No more options
            break
            ;;
    esac
done

# Handle cases where qemu and Debian use different arch names
case "$ARCH" in
    ppc64le)
        DEBARCH=ppc64el
        ;;
    aarch64)
        DEBARCH=arm64
        ;;
    arm)
        DEBARCH=armel
        ;;
    x86_64)
        DEBARCH=amd64
        ;;
    *)
        DEBARCH=$ARCH
        ;;
esac

# Foreign architecture

FOREIGN=false
if [ $ARCH != $(uname -m) ]; then
    # i386 on an x86_64 host is exempted, as we can run i386 binaries natively
    if [ $ARCH != "i386" -o $(uname -m) != "x86_64" ]; then
        FOREIGN=true
    fi
fi

if [ $FOREIGN = "true" ]; then
    # Check for according qemu static binary
    if ! which qemu-$ARCH-static; then
        echo "Please install qemu static binary for architecture $ARCH (package 'qemu-user-static' on Debian/Ubuntu/Fedora)"
        exit 1
    fi
    # Check for according binfmt entry
    if [ ! -r /proc/sys/fs/binfmt_misc/qemu-$ARCH ]; then
        echo "binfmt entry /proc/sys/fs/binfmt_misc/qemu-$ARCH does not exist"
        exit 1
    fi
fi

# Double check KERNEL when PERF is enabled
if [ $PERF = "true" ] && [ -z ${KERNEL+x} ]; then
    echo "Please set KERNEL environment variable when PERF is enabled"
    exit 1
fi

# If full feature is chosen, install more packages
if [ $FEATURE = "full" ]; then
    PREINSTALL_PKGS=$PREINSTALL_PKGS","$ADD_PACKAGE
fi

DIR=$RELEASE
sudo rm -rf $DIR
sudo mkdir -p $DIR
sudo chmod 0755 $DIR

# 1. debootstrap stage

DEBOOTSTRAP_PARAMS="--arch=$DEBARCH --include=$PREINSTALL_PKGS --components=main,contrib,non-free,non-free-firmware $RELEASE $DIR"
if [ $FOREIGN = "true" ]; then
    DEBOOTSTRAP_PARAMS="--foreign $DEBOOTSTRAP_PARAMS"
fi

# riscv64 is hosted in the debian-ports repository
# debian-ports doesn't include non-free, so we exclude firmware-atheros
if [ $DEBARCH == "riscv64" ]; then
    DEBOOTSTRAP_PARAMS="--keyring /usr/share/keyrings/debian-ports-archive-keyring.gpg --exclude firmware-atheros $DEBOOTSTRAP_PARAMS http://deb.debian.org/debian-ports"
fi

# debootstrap may fail for EoL Debian releases
RET=0
sudo --preserve-env=http_proxy,https_proxy,ftp_proxy,no_proxy debootstrap $DEBOOTSTRAP_PARAMS || RET=$?

if [ $RET != 0 ] && [ $DEBARCH != "riscv64" ]; then
    # Try running debootstrap again using the Debian archive
    DEBOOTSTRAP_PARAMS="--keyring /usr/share/keyrings/debian-archive-removed-keys.gpg $DEBOOTSTRAP_PARAMS https://archive.debian.org/debian-archive/debian/"
    sudo --preserve-env=http_proxy,https_proxy,ftp_proxy,no_proxy debootstrap $DEBOOTSTRAP_PARAMS
fi

# 2. debootstrap stage: only necessary if target != host architecture

if [ $FOREIGN = "true" ]; then
    sudo cp $(which qemu-$ARCH-static) $DIR/$(which qemu-$ARCH-static)
    sudo chroot $DIR /bin/bash -c "/debootstrap/debootstrap --second-stage"
fi

# Set some defaults and enable promtless ssh to the machine for root.
sudo sed -i '/^root/ { s/:x:/::/ }' $DIR/etc/passwd
echo 'T0:23:respawn:/sbin/getty -L ttyS0 115200 vt100' | sudo tee -a $DIR/etc/inittab
printf '\nauto eth0\niface eth0 inet dhcp\n' | sudo tee -a $DIR/etc/network/interfaces
echo '/dev/root / ext4 defaults 0 0' | sudo tee -a $DIR/etc/fstab
echo 'debugfs /sys/kernel/debug debugfs defaults 0 0' | sudo tee -a $DIR/etc/fstab
echo 'securityfs /sys/kernel/security securityfs defaults 0 0' | sudo tee -a $DIR/etc/fstab
echo 'configfs /sys/kernel/config/ configfs defaults 0 0' | sudo tee -a $DIR/etc/fstab
echo 'binfmt_misc /proc/sys/fs/binfmt_misc binfmt_misc defaults 0 0' | sudo tee -a $DIR/etc/fstab
#echo -en "127.0.0.1\tlocalhost\n" | sudo tee $DIR/etc/hosts
echo -en "127.0.0.1\tvm1\n" | sudo tee $DIR/etc/hosts
echo "nameserver 8.8.8.8" | sudo tee -a $DIR/etc/resolv.conf
echo "vm1" | sudo tee $DIR/etc/hostname
ssh-keygen -f $RELEASE.id_rsa -t rsa -N ''
sudo mkdir -p $DIR/root/.ssh/
cat $RELEASE.id_rsa.pub | sudo tee $DIR/root/.ssh/authorized_keys

# Add perf support
if [ $PERF = "true" ]; then
    cp -r $KERNEL $DIR/tmp/
    BASENAME=$(basename $KERNEL)
    sudo chroot $DIR /bin/bash -c "apt-get update; apt-get install -y flex bison python-dev libelf-dev libunwind8-dev libaudit-dev libslang2-dev libperl-dev binutils-dev liblzma-dev libnuma-dev"
    sudo chroot $DIR /bin/bash -c "cd /tmp/$BASENAME/tools/perf/; make"
    sudo chroot $DIR /bin/bash -c "cp /tmp/$BASENAME/tools/perf/perf /usr/bin/"
    rm -r $DIR/tmp/$BASENAME
fi

# Add udev rules for custom drivers.
# Create a /dev/vim2m symlink for the device managed by the vim2m driver
echo 'ATTR{name}=="vim2m", SYMLINK+="vim2m"' | sudo tee -a $DIR/etc/udev/rules.d/50-udev-default.rules

# ocfs2
sudo mkdir -p $DIR/etc/ocfs2
sudo tee $DIR/etc/ocfs2/cluster.conf << EOF
cluster:
    node_count = 2
    name = ocfs2cluster

node:
    name = vm1
    cluster = ocfs2cluster
    number = 0
    ip_port = 7777
    ip_address = 192.168.100.2

node:
    name = vm2
    cluster = ocfs2cluster
    number = 1
    ip_port = 7777
    ip_address = 192.168.100.3
EOF

# Update o2cb configuration
sudo sed -i 's/O2CB_ENABLED=false/O2CB_ENABLED=true/' $DIR/etc/default/o2cb
sudo sed -i 's/O2CB_BOOTCLUSTER=.*/O2CB_BOOTCLUSTER=ocfs2cluster/' $DIR/etc/default/o2cb


# drbd
sudo mkdir -p $DIR/etc/drbd.d
sudo tee $DIR/etc/drbd.d/shared.res << EOF
resource shared {
    protocol C;
    net {
        allow-two-primaries;
        sndbuf-size 4M;          # Увеличиваем размер буфера для отправки
        rcvbuf-size 4M;          # Увеличиваем размер буфера для получения
        max-buffers 20000;         # Увеличиваем количество сетевых буферов
        max-epoch-size 20000;      # Увеличиваем размер транзакции
        unplug-watermark 20000;    # Оптимизируем задержку записи
    }
    disk {
        no-disk-barrier;
        no-disk-flushes;
    }
    startup {
        become-primary-on both;
    }
    on vm1 {
        device    /dev/drbd1;
        disk      /dev/vda;
        address   192.168.100.2:7789;
        meta-disk internal;
    }
    on vm2 {
        device    /dev/drbd1;
        disk      /dev/vda;
        address   192.168.100.3:7789;
        meta-disk internal;
    }
}
EOF

# setup network
sudo tee -a $DIR/etc/rc.local << EOF
ip addr add 192.168.100.2/24 dev eth0
ip link set eth0 up
exit 0
EOF
sudo chmod +x $DIR/etc/rc.local

# Enable rc.local through systemd
sudo tee $DIR/etc/systemd/system/rc-local.service << EOF
[Unit]
Description=/etc/rc.local Compatibility
ConditionPathExists=/etc/rc.local
After=network.target

[Service]
Type=forking
ExecStart=/etc/rc.local start
TimeoutSec=0
StandardOutput=tty
RemainAfterExit=yes
SysVStartPriority=99

[Install]
WantedBy=multi-user.target
EOF

# Disable panic on warnings in sysctl.conf
echo "kernel.panic_on_warn = 0" | sudo tee -a $DIR/etc/sysctl.conf

# Build a disk image
dd if=/dev/zero of=$RELEASE.img bs=1M seek=$SEEK count=1
sudo mkfs.ext4 -F $RELEASE.img
sudo mkdir -p /mnt/$DIR
sudo mount -o loop $RELEASE.img /mnt/$DIR
sudo cp -a $DIR/. /mnt/$DIR/.
sudo umount /mnt/$DIR
