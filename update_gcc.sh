gcc_version=9.3.0
base_path=/app

yum -y install bzip2 gcc gcc-c++

# virtual memory
dd if=/dev/vda1 of=/opt/swap bs=1G count=4
chmod 600 /opt/swap
mkswap /opt/swap
swapon /opt/swap
free -m

cd ${base_path}
wget http://mirror.hust.edu.cn/gnu/gcc/gcc-${gcc_version}/gcc-${gcc_version}.tar.gz
tar zxvf gcc-${gcc_version}.tar.gz

cd ${base_path}/gcc-${gcc_version}
./contrib/download_prerequisites

mkdir build && cd build
../configure -prefix=/usr/local --enable-checking=release --enable-languages=c,c++ --disable-multilib

make -j8
make install

find / -name "libstdc++.so*"
cp ${base_path}/gcc-${gcc_version}/build/stage1-x86_64-pc-linux-gnu/libstdc++-v3/src/.libs/libstdc++.so.6.0.28 /usr/lib64
rm -rf /usr/lib64/libstdc++.so.6
ln -s /usr/lib64/libstdc++.so.6.0.28 /usr/lib64/libstdc++.so.6

# reboot