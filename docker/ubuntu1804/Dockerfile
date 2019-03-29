FROM ubuntu:18.04

RUN apt-get update && apt-get install -y wget gnupg2
RUN ["/bin/bash", "-c","set -o pipefail && wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -"]

RUN echo "deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-8 main\n" >> /etc/apt/sources.list && \
    apt-get update -y && apt-get install -y llvm-8-dev \
    libllvm8 \
    llvm-8-tools \
    llvm-8 \
    libclang-common-8-dev \
    clang \
    cmake \
    make

ENV BUILD_DIR /opt/build
ENV SOURCE_DIR /opt/src
ENV INSTALL_DIR /opt/install
ENV BUILD_TYPE Release
ENV BUILD_SHARED_LIBS ON

COPY scripts/build.sh /opt/bin/build.sh
