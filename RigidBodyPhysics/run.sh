#/bin/sh

# For my own internal use.

set -e

if [ $# -eq 0 ]
then
    BUILD_TYPE=Debug
    BUILD_DIRECTORY=build
else
    if [ "$1" == "release" ]
    then
        BUILD_TYPE=Release
        BUILD_DIRECTORY=build-release
    elif [ "$1" == "check" ]
    then
        cppcheck -j$(nproc) --enable=all --inconclusive --project=compile_commands.json \
            -i "*lib*" --suppress="*:*lib*" --suppress="missingInclude" --suppress="missingIncludeSystem"
        exit $?
    else
        echo "Unrecognized argument: $1"
        exit 1
    fi
fi

mkdir -p "$BUILD_DIRECTORY"

export CC=clang
export CXX=clang++

cmake -B "$BUILD_DIRECTORY" -DCMAKE_BUILD_TYPE=$BUILD_TYPE -G Ninja
time cmake --build "$BUILD_DIRECTORY"

export LD_PRELOAD=/usr/lib/clang/21/lib/linux/libclang_rt.asan-x86_64.so

./$BUILD_DIRECTORY/demo_test
cd "$BUILD_DIRECTORY"
ASAN_OPTIONS=detect_leaks=0 ./demo
