FROM debian@sha256:d07d1b51c39f51188e60be9b64e6bf769fa94e187f092bc32b91305cfa34ba5a AS toolchain

# Immutable repositories pin transitive dependencies as well as build tools.
RUN rm /etc/apt/sources.list.d/debian.sources && \
    printf '%s\n' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20261014T000000Z/ trixie main' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/20261014T000000Z/ trixie-security main' \
      > /etc/apt/sources.list && \
    apt-get -o Acquire::Retries=3 update && \
    apt-get install -y --no-install-recommends \
      g++=4:14.2.0-1 cmake=3.31.6-2 ninja-build pkg-config python3 \
      libbz2-dev libjpeg62-turbo-dev zlib1g-dev libssl-dev \
      libsdl1.2-compat-dev libgl-dev libglu1-mesa-dev libglew-dev \
      libopenal-dev libsndfile1-dev libmpg123-dev binutils file ca-certificates && \
    dpkg-query -W -f='${Package}\t${Version}\t${Architecture}\n' > /toolchain-packages.tsv && \
    rm -rf /var/lib/apt/lists/*

FROM toolchain AS build
COPY . /src
WORKDIR /src
ARG JOBS=4
ENV SOURCE_DATE_EPOCH=1754343618 TZ=UTC LC_ALL=C
RUN --mount=type=cache,target=/build,sharing=locked \
    cmake -S /src -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      '-DCMAKE_C_FLAGS=-march=armv8-a -mtune=cortex-a35 -fsigned-char -ffile-prefix-map=/src=.' \
      '-DCMAKE_CXX_FLAGS=-march=armv8-a -mtune=cortex-a35 -fsigned-char -ffile-prefix-map=/src=.' \
      -DSERVERONLY=OFF -DNO_ASM=ON -DNO_GTK=ON -DNO_GL=OFF \
      -DNO_SOUND=OFF -DUSE_OPENAL=ON -DRELEASE_WITH_DEBUG_FILE=ON && \
    cmake --build /build --parallel "$JOBS" > /build/build.log 2>&1 && \
    python3 /src/tools/arm64/export.py || \
    { cat /build/build.log 2>/dev/null; exit 1; }

FROM scratch AS artifacts
COPY --from=build /export/ /
