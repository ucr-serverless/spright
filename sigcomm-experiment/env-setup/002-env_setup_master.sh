#/bin/bash
# This script can be run with non-root user

echo "Installing libbpf"
cd /mydata # Use the extended disk with enough space

git clone --single-branch https://github.com/libbpf/libbpf.git
cd libbpf
git switch --detach v0.6.0
cd src
make -j $(nproc)
sudo make install
echo "/usr/lib64/" | sudo tee -a /etc/ld.so.conf
sudo ldconfig
cd ../..

echo "Installing DPDK"
cd /mydata # Use the extended disk with enough space

git clone --single-branch git://dpdk.org/dpdk
cd dpdk
git switch --detach v21.11
meson build
cd build
ninja
sudo ninja install
sudo ldconfig
cd ../..

echo "Set up hugepages"
sudo sysctl -w vm.nr_hugepages=16384

echo "build SPRIGHT"
cd /mydata # Use the extended disk with enough space

cd spright/src/cstl && make
cd ../../ && make