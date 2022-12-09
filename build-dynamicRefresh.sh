git submodule sync
git submodule update --init --recursive

git submodule sync
git submodule update --init --recursive

mkdir -p build
cd build

cmake ..
cmake --build .
#cmake --build . --parallel

