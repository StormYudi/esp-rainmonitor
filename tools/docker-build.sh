#!/usr/bin/env bash
# Build firmware with the official Espressif IDF container. No local toolchain.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${IDF_DOCKER_IMAGE:-espressif/idf:v6.1}"
HOST_PROXY="${all_proxy:-${ALL_PROXY:-${https_proxy:-${HTTPS_PROXY:-}}}}"
# The daemon pull uses the host proxy. The container must reach the host.
CTR_PROXY="${HOST_PROXY//127.0.0.1/host.docker.internal}"
CTR_PROXY="${CTR_PROXY//localhost/host.docker.internal}"

docker_env=()
if [[ -n "$CTR_PROXY" ]]; then
  docker_env+=(-e "all_proxy=$CTR_PROXY" -e "ALL_PROXY=$CTR_PROXY")
  docker_env+=(-e "http_proxy=$CTR_PROXY" -e "https_proxy=$CTR_PROXY")
  docker_env+=(-e "HTTP_PROXY=$CTR_PROXY" -e "HTTPS_PROXY=$CTR_PROXY")
fi

run_args=(
  --rm
  -v "$ROOT:/project"
  -w /project
  -u "$(id -u):$(id -g)"
  -e HOME=/tmp
)
if [[ ${#docker_env[@]} -gt 0 ]]; then
  run_args+=("${docker_env[@]}")
fi

exec docker run "${run_args[@]}" "$IMAGE" bash -lc 'idf.py --preview build && python tools/package.py'
