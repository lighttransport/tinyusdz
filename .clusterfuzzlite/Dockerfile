FROM gcr.io/oss-fuzz-base/base-builder:v1
RUN apt-get update && apt-get install -y make autoconf automake libtool meson ninja-build
COPY . $SRC/lightusd
WORKDIR $SRC/lightusd
COPY .clusterfuzzlite/build.sh $SRC/
