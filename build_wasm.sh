rm -rf build
mkdir build
cd build

# Make the path to emscripten available to cmake.
export EMSCRIPTEN=/app/emscripten

# Emscripten.cmake can be found within your Emscripten installation directory,
# it should be the subdir: cmake/Modules/Platform/Emscripten.cmake
cmake ../ -DCMAKE_TOOLCHAIN_FILE=${EMSCRIPTEN}/cmake/Modules/Platform/Emscripten.cmake -DDRACO_WASM=ON -DDRACO_VERBOSE=1

# Build the WebAssembly decoder.
make -j16

# Run the Javascript wrapper through Closure.
# https://mvnrepository.com/artifact/com.google.javascript/closure-compiler
java -jar ../closure-compiler-v20201207.jar --compilation_level SIMPLE --js draco_decoder.js --js_output_file draco_wasm_wrapper.js
