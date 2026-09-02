#!/bin/sh

set -eu

THIS_SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd -P)

export REDIS_CONTAINER_NAME=${REDIS_CONTAINER_NAME:-nestdaq-redis-8.2.7}
export REDIS_IMAGE=${REDIS_IMAGE:-docker.io/library/redis:8.2.7}
export REDIS_DATA_VOLUME=${REDIS_DATA_VOLUME:-"${REDIS_CONTAINER_NAME}-data"}
export REDIS_DATA_DIR=${REDIS_DATA_DIR:-"${THIS_SCRIPT_DIR}/redis-8.2.7-data"}
export REDIS_ARGS_MODE=${REDIS_ARGS_MODE:-argv}

exec "${THIS_SCRIPT_DIR}/run-redis-stack-server.sh"
