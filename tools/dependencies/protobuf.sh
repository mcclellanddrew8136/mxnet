#!/usr/bin/env bash

# This script builds the static library of protobuf along with protoc, that can be used as dependency of mxnet.
PROTOBUF_VERSION=3.5.1
if [[ $PLATFORM == 'darwin' ]]; then
    DY_EXT="dylib"
else
    DY_EXT="so"
fi

LIBPROTOBUF="$DEPS_PATH/lib/libprotobuf.$DY_EXT"
LIBPROTOC="$DEPS_PATH/lib/libprotoc.$DY_EXT"
if [[ ! -e $LIBPROTOBUF ]] || [[ ! -e $LIBPROTOC ]]; then
    # Download and build protobuf
    >&2 echo "Building protobuf..."
    curl -s -L https://github.com/google/protobuf/archive/v$PROTOBUF_VERSION.zip -o $DEPS_PATH/protobuf.zip
    unzip -q $DEPS_PATH/protobuf.zip -d $DEPS_PATH
    cd $DEPS_PATH/protobuf-$PROTOBUF_VERSION
    ./autogen.sh
    ./configure -prefix=$DEPS_PATH
    make
    make install
    cd -
fi
