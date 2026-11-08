#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Project-configured JieLi toolchain and Docker/Colima environment.
# Source this file before tools/build.py or tools/check_plaits_target.py.
# Run `source tools/device_env.sh` from the repository root.
_device_root=$(pwd -P)
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$_device_root/build/device-tools/toolchain/current}"
export AC79_SDK="${AC79_SDK:-$_device_root/build/device-tools/AC79_SDK}"
export COLIMA_CACHE_HOME="${COLIMA_CACHE_HOME:-$_device_root/build/device-tools/cache}"
# Lima and Colima both create Unix sockets under their state directories. Keep
# those paths short enough for macOS AF_UNIX limits; package inputs and caches
# remain under build/device-tools.
export COLIMA_HOME="${COLIMA_HOME:-/private/tmp/fm1-dinghy-colima}"
export LIMA_HOME="${LIMA_HOME:-/private/tmp/fm1-dinghy-lima}"
export DOCKER_CONFIG="${DOCKER_CONFIG:-$_device_root/build/device-tools/docker}"
export DOCKER_CONTEXT="${DOCKER_CONTEXT:-colima-dinghy}"
export JIELI_DOCKER="${JIELI_DOCKER:-1}"
export JIELI_DOCKER_IMAGE="${JIELI_DOCKER_IMAGE:-debian:bookworm-slim@sha256:7c7b2c966bc9ee8cedfeef67e0e279108992c77681fa595db4a9d65c06ccc587}"
