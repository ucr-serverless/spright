# SPRIGHT

## Install dependencies (apt)
```bash
sudo apt install -y flex bison build-essential dwarves libssl-dev libelf-dev \
                    libnuma-dev pkg-config python3-pip python3-pyelftools \
                    libconfig-dev golang clang gcc-multilib
```

## Install dependencies (pip)
```bash
sudo pip3 install meson ninja
```

## Install Linux
```bash
wget --no-hsts https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.16.tar.xz
tar -xf linux-5.16.tar.xz
cd linux-5.16
make olddefconfig
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str SYSTEM_REVOCATION_KEYS ""
make -j $(nproc)
find -name *.ko -exec strip --strip-unneeded {} +
sudo make modules_install -j $(nproc)
sudo make install
cd ..
```

## Reboot system
```bash
sudo reboot
```

## Install libbpf
```bash
git clone --single-branch https://github.com/libbpf/libbpf.git
cd libbpf
git switch --detach v0.6.0
cd src
make -j $(nproc)
sudo make install
echo "/usr/lib64/" | sudo tee -a /etc/ld.so.conf
sudo ldconfig
cd ../..
```

## Install DPDK
```bash
git clone --single-branch git://dpdk.org/dpdk
cd dpdk
git switch --detach v21.11
meson build
cd build
ninja
sudo ninja install
sudo ldconfig
cd ../..
```

## Set up hugepages
```bash
sudo sysctl -w vm.nr_hugpages=16384
```

## Build SPRIGHT
```bash
make
```

## Compile the eBPF code into an object file
```bash
cd ebpf/
clang -g -O2 -c -target bpf -o sk_msg_kern.o sk_msg_kern.c

# Copy sk_msg_kern.o to obj/
cp sk_msg_kern.o ../obj/

# Debugging purpose: output bpf_printk()
sudo cat  /sys/kernel/debug/tracing/trace_pipe
```