ARG UBUNTU_VERSION=22.04

FROM ubuntu:${UBUNTU_VERSION} AS build

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        ninja-build \
        python3-dev \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir --upgrade pip \
    && /opt/venv/bin/pip install --no-cache-dir "nanobind>=2.0" "numpy>=1.22" pytest
ENV PATH=/opt/venv/bin:${PATH}

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPython_EXECUTABLE=/opt/venv/bin/python \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure \
    && cmake --install build --prefix /opt/terraingen \
    && pip wheel --no-cache-dir --no-deps . -w /opt/wheels

FROM ubuntu:${UBUNTU_VERSION} AS dev

LABEL org.opencontainers.image.source="https://github.com/alexmaietta/terraingen" \
      org.opencontainers.image.description="TerrainGen: the library, headers, CMake package, CLI and Python package" \
      org.opencontainers.image.licenses="BSD-3-Clause"

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/terraingen /opt/terraingen
COPY --from=build /opt/wheels /opt/wheels
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir /opt/wheels/*.whl
ENV CMAKE_PREFIX_PATH=/opt/terraingen \
    PATH=/opt/venv/bin:${PATH}
