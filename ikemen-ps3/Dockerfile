# Build environment for Ikemen PS3: Ubuntu + the pre-built ps3dev toolchain (compiler + PSL1GHT).
# Used by build.sh. Nothing is compiled here, so the image builds in a few minutes.
FROM --platform=linux/amd64 ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates curl autoconf automake bison flex gcc libelf-dev make texinfo \
        libncurses5-dev patch python3 python-is-python3 wget zlib1g-dev libtool-bin \
        python3-dev bzip2 libgmp3-dev pkg-config libssl-dev \
    && rm -rf /var/lib/apt/lists/*

COPY tools/fetch-ps3dev.sh /tmp/fetch-ps3dev.sh
RUN sh /tmp/fetch-ps3dev.sh /usr/local && rm /tmp/fetch-ps3dev.sh

ENV PS3DEV=/usr/local/ps3dev
ENV PSL1GHT=/usr/local/ps3dev
ENV PATH=${PATH}:/usr/local/ps3dev/bin:/usr/local/ps3dev/ppu/bin:/usr/local/ps3dev/spu/bin

WORKDIR /build
