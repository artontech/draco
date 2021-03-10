rm -rf build
mkdir build
cd build

cmake ../

# Build the WebAssembly decoder.
make -j16
