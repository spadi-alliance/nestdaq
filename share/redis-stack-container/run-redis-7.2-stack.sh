#!/bin/sh

set -eu

THIS_SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd -P)

export REDIS_CONTAINER_NAME=${REDIS_CONTAINER_NAME:-nestdaq-redis-7.2-stack}
export REDIS_IMAGE=${REDIS_IMAGE:-docker.io/redis/redis-stack:7.2.0-v20}
export REDIS_DATA_VOLUME=${REDIS_DATA_VOLUME:-"${REDIS_CONTAINER_NAME}-data"}
export REDIS_INSIGHT_VOLUME=${REDIS_INSIGHT_VOLUME:-"${REDIS_CONTAINER_NAME}-redisinsight"}
export REDIS_DATA_DIR=${REDIS_DATA_DIR:-"${THIS_SCRIPT_DIR}/redis-7.2-stack-data"}
export REDIS_INSIGHT_DATA_DIR=${REDIS_INSIGHT_DATA_DIR:-"${THIS_SCRIPT_DIR}/redis-7.2-redisinsight-data"}

exec "${THIS_SCRIPT_DIR}/run-redis-stack.sh"
