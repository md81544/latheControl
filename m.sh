#!/bin/bash

mkdir -p build
BUILD_TYPE=debug
RUN_TESTS=n

while [[ "$1" != "" ]]; do
    case "$1" in
        "debug"|"fake" )
            BUILD_TYPE=debug
            ;;
        "clean" )
            rm -rf build/*
            exit 0
            ;;
        "release" )
            BUILD_TYPE=release
            ;;
        "test" )
            BUILD_TYPE=debug
            RUN_TESTS=y
            ;;
        "--verbose"|"-v" )
            VERBOSE='VERBOSE=1'
            ;;
        "sub" )
            git submodule update --init --recursive
            exit 0
            ;;
        "-help"|"--help"|"-h"|"-?")
            echo "./m.sh [debug|release] [--rebuild]"
            exit 1
            ;;
        *)
            echo "Unrecognized option $1"
            exit 2
            ;;
    esac
    shift
done

if [[ "$1" != "" ]]; then
    BUILD_TYPE=${1}
fi

if [[ "${BUILD_TYPE}" == "release" ]]; then
    if [[ -f "build/DEBUG" ]]; then
        rm -rf build/*
    fi
    touch build/RELEASE
fi
if [[ "${BUILD_TYPE}" == "debug" ]]; then
    if [[ -f "build/RELEASE" ]]; then
        rm -rf build/*
    fi
    touch build/DEBUG
fi

cd build
cmake ..
make ${BUILD_TYPE} ${VERBOSE}
cd ..
rm -f lc
ln -s build/lc

if [[ "${RUN_TESTS}" == "y" ]]; then
    build/test/unit_test -d y
fi
