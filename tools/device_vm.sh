#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Start, inspect, or stop the project-local Linux VM used by device builds.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
. "$ROOT/tools/device_env.sh"
PROFILE=dinghy

case "${1:-start}" in
  start)
    mkdir -p "$COLIMA_HOME" "$COLIMA_CACHE_HOME" "$LIMA_HOME" "$DOCKER_CONFIG"
    colima --profile "$PROFILE" start \
      --vm-type "${JIELI_VM_TYPE:-vz}" --arch x86_64 \
      --cpus "${JIELI_VM_CPU:-4}" --memory "${JIELI_VM_MEMORY:-8}" \
      --disk "${JIELI_VM_DISK:-64}"
    docker context use "$DOCKER_CONTEXT" >/dev/null
    docker info >/dev/null
    ;;
  status)
    colima --profile "$PROFILE" status
    docker context ls
    ;;
  stop)
    colima --profile "$PROFILE" stop
    ;;
  *)
    echo "usage: $0 [start|status|stop]" >&2
    exit 2
    ;;
esac
