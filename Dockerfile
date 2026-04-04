FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    bison \
    build-essential \
    ca-certificates \
    clang \
    cmake \
    curl \
    file \
    flex \
    g++ \
    gcc \
    git \
    libc6-dev \
    llvm \
    make \
    ninja-build \
    python3 \
    python3-pip \
    ripgrep \
    unzip \
    zip \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["bash"]
