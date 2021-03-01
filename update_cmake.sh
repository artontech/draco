cmake_version=3.19.2
base_path=/app

cd ${base_path}
wget https://github.com/Kitware/CMake/releases/download/v${cmake_version}/cmake-${cmake_version}-Linux-x86_64.tar.gz
tar zxvf cmake-${cmake_version}-Linux-x86_64.tar.gz

echo export CMAKE_HOME=${base_path}/cmake-${cmake_version}-Linux-x86_64 > /etc/profile.d/cmake.sh
echo 'export PATH=$PATH:$CMAKE_HOME/bin' >> /etc/profile.d/cmake.sh
source /etc/profile

cmake --version