# Builder
ARG UBUNTU_VERSION=22.04

FROM ubuntu:${UBUNTU_VERSION} AS build

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libgdal-dev \
        libproj-dev \
        libwayland-dev \
        libxkbcommon-dev \
        ninja-build \
        pkg-config \
        xorg-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure \
    && cmake --install build --prefix /opt/terraingen

# Runtime
FROM ubuntu:${UBUNTU_VERSION} AS runtime

# GDAL runtime libraries get added here once terraingen::io links against GDAL.
COPY --from=build /opt/terraingen /opt/terraingen
ENV PATH=/opt/terraingen/bin:${PATH}

ENTRYPOINT ["terraingen"]
