#/bin/sh

# For my own internal use.

set -e

if [ $# -eq 0 ]
then
    BUILD_TYPE=Debug
    BUILD_DIRECTORY=Build
else
    if [ "$1" == "release" ]
    then
        BUILD_TYPE=Release
        # BUILD_TYPE=RelWithDebInfo
        BUILD_DIRECTORY=BuildRelease
    elif [ "$1" == "check" ]
    then
        cppcheck -j$(nproc) --enable=all --inconclusive --project=compile_commands.json \
            -i "*lib*" --suppress="*:*lib*" --suppress="missingInclude" --suppress="missingIncludeSystem"
        exit $?
    else
        echo "unrecognized argument: $1"
        exit 1
    fi
fi

mkdir -p "$BUILD_DIRECTORY"

export CC=clang
export CXX=clang++

cmake -B "$BUILD_DIRECTORY" -DCMAKE_BUILD_TYPE=$BUILD_TYPE -G Ninja
time cmake --build "$BUILD_DIRECTORY"

if [ "$BUILD_TYPE" == "Debug" ]
then
    CLANG_VERSION=$(clang++ --version | rg -or '$1' 'version (\d\d)')
    export LD_PRELOAD=/usr/lib/clang/$CLANG_VERSION/lib/linux/libclang_rt.asan-x86_64.so
fi

cd "$BUILD_DIRECTORY"

if [ "$BUILD_TYPE" == "Debug" ]
then
    ./DemoTest
fi

# For protect_shadow_gap=0 see this (probably only relevant on NVIDIA hardware):
# https://forums.developer.nvidia.com/t/vkcreatedevice-fails-when-enabling-vk-khr-acceleration-structure-and-vk-khr-ray-query-and-address-sanitizer-on-linux/361757/2
# https://stackoverflow.com/questions/55750700/opencl-usable-when-compiling-host-application-with-address-sanitizer
ASAN_OPTIONS=detect_leaks=0:protect_shadow_gap=0 ./Demo
