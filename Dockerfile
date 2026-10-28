FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    qt6-base-dev \
    qt6-websockets-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j2

CMD ["sh", "-c", "exec ./build/appserver --port \"${PORT:-10000}\" --data-file \"${DATA_FILE:-/data/state.json}\""]
