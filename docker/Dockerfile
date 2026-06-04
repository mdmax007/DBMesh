# ── Stage 1: Builder ────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake ninja-build \
    clang-18 \
    libboost-all-dev \
    libyaml-cpp-dev \
    libsqlite3-dev \
    libssl-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

ENV CC=clang-18 CXX=clang++-18

WORKDIR /src
COPY . .

RUN cmake -GNinja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang-18 \
      -DCMAKE_CXX_COMPILER=clang++-18 \
      -DDBMESH_BUILD_TESTS=OFF \
      -B build && \
    ninja -C build dbmesh dbmeshctl

# ── Stage 2: Runtime ────────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

# yaml-cpp is header-only for linking; only the .so is needed at runtime
RUN apt-get update && apt-get install -y --no-install-recommends libyaml-cpp0.7 \
    || apt-get install -y --no-install-recommends libyaml-cpp0.8 \
    && rm -rf /var/lib/apt/lists/* || true

COPY --from=builder /src/build/dbmesh    /usr/local/bin/dbmesh
COPY --from=builder /src/build/dbmeshctl /usr/local/bin/dbmeshctl

# MySQL frontend, PostgreSQL frontend, Admin API, Prometheus metrics, Gossip (UDP)
EXPOSE 3306 5432 8080 9090 7946/udp 7946/tcp

ENTRYPOINT ["dbmesh"]
CMD ["--config", "/etc/dbmesh/dbmesh.yaml"]
