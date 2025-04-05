# syntax=docker/dockerfile:1.1-experimental

FROM            debian:bookworm

RUN             --mount=type=bind,target=/tmp/scripts,source=scripts bash /tmp/scripts/build.sh

ENV             DEBIAN_FRONTEND newt
