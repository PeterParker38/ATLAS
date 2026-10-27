FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake ninja-build libasio-dev ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY include/ include/
COPY src/ src/
COPY apps/ apps/
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build /build --parallel 2 && cmake --install /build --prefix /install

FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 atlas && useradd --uid 10001 --gid atlas --no-create-home atlas
COPY --from=build /install/bin/ /usr/local/bin/
USER 10001:10001
EXPOSE 7070
HEALTHCHECK --interval=10s --timeout=4s --start-period=3s --retries=3 \
    CMD ["atlas-cli", "--timeout-ms", "2000", "ping"]
ENTRYPOINT ["atlas-node"]
CMD ["--memory-demo", "--bind", "0.0.0.0", "--port", "7070"]
