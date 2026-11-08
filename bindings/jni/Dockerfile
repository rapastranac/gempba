# Build environment for the Linux JNI library: glibc 2.28 (AlmaLinux 8) and GCC 14, so the library loads on RHEL 8 and
# newer. scripts/build_jar_linux.sh builds it with bindings/jni as the context.
FROM quay.io/pypa/manylinux_2_28_x86_64@sha256:9555ba6c479fa69a2aa80ad1dc57154a60d562b19df159361d9379a02beb1e36

RUN dnf install -y openmpi-devel && dnf clean all

ARG JDK_URL=https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.3%2B9/OpenJDK25U-jdk_x64_linux_hotspot_25.0.3_9.tar.gz
ARG JDK_SHA256=69264a7a211bf5029830d07bc3370f879769d62ebc5b5488e90c9343a2da0e1f
RUN curl -fsSL -o /tmp/jdk.tar.gz "$JDK_URL" \
    && echo "$JDK_SHA256  /tmp/jdk.tar.gz" | sha256sum -c --quiet - \
    && mkdir /opt/jdk \
    && tar -xzf /tmp/jdk.tar.gz -C /opt/jdk --strip-components=1 \
    && rm /tmp/jdk.tar.gz

COPY build_deps.sh /tmp/build_deps.sh
RUN bash /tmp/build_deps.sh /opt/gempba-deps && rm /tmp/build_deps.sh

ENV JAVA_HOME=/opt/jdk \
    CMAKE_PREFIX_PATH=/opt/gempba-deps \
    PKG_CONFIG_PATH=/opt/gempba-deps/lib/pkgconfig \
    PATH=/usr/lib64/openmpi/bin:$PATH
