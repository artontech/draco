base_path=/app

# install llvm
llvm_path=${base_path}/llvm
git clone https://github.com.cnpmjs.org/llvm/llvm-project.git $llvm_path
mkdir ${llvm_path}/build
cd ${llvm_path}/build/
cmake ../llvm -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS='lld;clang' -DLLVM_TARGETS_TO_BUILD="host;WebAssembly" -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF -DCMAKE_C_COMPILER=/usr/local/bin/gcc
cmake --build . -j8

# install binaryen
binaryen_path=${base_path}/binaryen
git clone https://github.com.cnpmjs.org/WebAssembly/binaryen.git $binaryen_path
cd ${binaryen_path}
cmake . -DCMAKE_C_COMPILER=/usr/local/bin/gcc
make -j8
ln -s ${binaryen_path}/lib/libbinaryen.so /usr/lib64/libbinaryen.so

# install node.js
node_version=14.15.4
cd ${base_path}
wget https://nodejs.cnpmjs.org/dist/v${node_version}/node-v${node_version}-linux-x64.tar.xz
tar xvf node-v${node_version}-linux-x64.tar.xz
ln -s ${base_path}/node-v${node_version}-linux-x64/bin/node /usr/bin/node
ln -s ${base_path}/node-v${node_version}-linux-x64/bin/npm /usr/bin/npm
node -v

# Make the path to emscripten available to cmake.
emscripten_path=/app/emscripten
git clone https://github.com.cnpmjs.org/emscripten-core/emscripten.git $emscripten_path
sed -i "s#os.getenv('LLVM', '/usr/bin')#os.getenv('LLVM', '${llvm_path}/build/bin')#g" ${emscripten_path}/.emscripten
sed -i "s#os.getenv('BINARYEN', '')#os.getenv('BINARYEN', '${binaryen_path}')#g" ${emscripten_path}/.emscripten
cd ${emscripten_path}
npm install

# install dependencies
draco_path=/app/draco
cd ${draco_path}