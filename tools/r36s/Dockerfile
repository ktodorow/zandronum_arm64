FROM debian@sha256:d07d1b51c39f51188e60be9b64e6bf769fa94e187f092bc32b91305cfa34ba5a AS build

RUN rm /etc/apt/sources.list.d/debian.sources && \
    printf '%s\n' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20261014T000000Z/ trixie main' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/20261014T000000Z/ trixie-security main' \
      > /etc/apt/sources.list && \
    apt-get -o Acquire::Retries=3 update && \
    apt-get install -y --no-install-recommends \
      gcc=4:14.2.0-1 cmake=3.31.6-2 make pkg-config libc6-dev patch \
      libdrm-dev libgbm-dev libegl-dev libgles-dev libglu1-mesa binutils ca-certificates && \
    mkdir -p /export/evidence /export/libs.aarch64 /export/gl4es.aarch64 && \
    dpkg-query -W -f='${Package}\t${Version}\t${Architecture}\n' > /export/evidence/runtime-packages.tsv

# Pinned upstream source; GOA_CLONE preset adds unnecessary CPU extensions.
ADD --checksum=sha256:b4e68ee01c45c114d89d0df290618ea7aa6f4078db11c276a893af39b9ad9b76 \
    https://codeload.github.com/ptitSeb/gl4es/tar.gz/a025615bc79ac43ec911dced8fe5b8eb45a0b72d /gl4es.tar.gz
COPY tools/r36s/gl4es-egl-platform-display.patch /egl-platform-display.patch
RUN mkdir /gl4es && tar -xf /gl4es.tar.gz -C /gl4es --strip-components=1 && \
    patch -d /gl4es -p1 < /egl-platform-display.patch && \
    cmake -S /gl4es -B /build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      '-DCMAKE_C_FLAGS=-march=armv8-a -mtune=cortex-a35' \
      -DNOX11=ON -DGLX_STUBS=OFF -DEGL_WRAPPER=ON -DGBM=ON -DDEFAULT_ES=2 && \
    { cmake --build /build --parallel 4 > /export/evidence/gl4es-build.log 2>&1 || \
      { tail -80 /export/evidence/gl4es-build.log; exit 1; }; } && \
    cp -L /gl4es/lib/libGL.so.1 /export/gl4es.aarch64/libGL.so.1 && \
    cp -L /gl4es/lib/libEGL.so.1 /export/gl4es.aarch64/libEGL.so.1 && \
    cp -L /usr/lib/aarch64-linux-gnu/libGLU.so.1 /export/libs.aarch64/ && \
    cp -L /usr/lib/aarch64-linux-gnu/libOpenGL.so.0 /export/libs.aarch64/ && \
    cp -L /usr/lib/aarch64-linux-gnu/libGLdispatch.so.0 /export/libs.aarch64/ && \
    cp /gl4es/LICENSE /export/evidence/gl4es-LICENSE && \
    cp /egl-platform-display.patch /export/evidence/ && \
    cp /build/CMakeCache.txt /export/evidence/gl4es-CMakeCache.txt && \
    printf '%s\n' a025615bc79ac43ec911dced8fe5b8eb45a0b72d > /export/evidence/gl4es-commit.txt && \
    for lib in /export/*aarch64/*; do \
      readelf -d "$lib" > "/export/evidence/$(basename "$lib").dynamic.txt"; \
    done

FROM scratch AS artifacts
COPY --from=build /export/ /
