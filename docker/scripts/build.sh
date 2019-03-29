#!/bin/sh

cd $BUILD_DIR
cmake $SOURCE_DIR \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DBUILD_SHARED_LIBS=$SHARED_LIBS \
        -DLLVM_BUILD_TOOLS=ON \
        -DLLVM_INCLUDE_TESTS=ON \
        -DCMAKE_INSTALL_PREFIX=/opt/install \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -G "Unix Makefiles"
cmake --build .
cmake --build . --target install
