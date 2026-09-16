# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

FROM ubuntu:24.04

ENV SRC=/src
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_INSTALLED_DIR=/opt/vcpkg-installed
ENV CMAKE_PREFIX_PATH=/opt/vcpkg-installed/x64-linux
ENV CPATH=/opt/vcpkg-installed/x64-linux/include

RUN apt-get update && \
    apt-get install --no-install-recommends -y \
      build-essential \
      ca-certificates \
      cmake \
      curl \
      git \
      clang \
      clang-tools-18 \
      libclang-rt-18-dev \
      libfuzzer-18-dev \
      libmagicenum-dev \
      libprotobuf-dev \
      libspdlog-dev \
      ninja-build \
      pkg-config \
      protobuf-compiler \
      python3 \
      tar \
      unzip \
      zip && \
    rm -rf /var/lib/apt/lists/*

COPY .clusterfuzzlite/vcpkg.json /tmp/cfl-vcpkg/vcpkg.json
RUN git clone --filter=blob:none --no-checkout https://github.com/microsoft/vcpkg.git $VCPKG_ROOT && \
    git -C $VCPKG_ROOT checkout cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3 && \
    $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics && \
    $VCPKG_ROOT/vcpkg install \
      --x-install-root=$VCPKG_INSTALLED_DIR \
      --x-manifest-root=/tmp/cfl-vcpkg && \
    rm -rf /tmp/cfl-vcpkg

COPY . $SRC/astl

WORKDIR $SRC/astl
COPY .clusterfuzzlite/build.sh /usr/local/bin/compile
