#!/bin/sh

set -eu

THIS_SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd -P)

CONTAINER_RUNTIME=${CONTAINER_RUNTIME:-docker}
REDIS_CONTAINER_NAME=${REDIS_CONTAINER_NAME:-nestdaq-redis-stack-server}
REDIS_CONTAINER_REPLACE=${REDIS_CONTAINER_REPLACE:-1}
REDIS_IMAGE=${REDIS_IMAGE:-docker.io/redis/redis-stack-server:7.4.0-v8}
REDIS_PORT=${REDIS_PORT:-6379}
REDIS_CONTAINER_RUN_FLAGS=${REDIS_CONTAINER_RUN_FLAGS:---rm -it}
REDIS_VOLUME_MODE=${REDIS_VOLUME_MODE:-bind}
REDIS_DATA_VOLUME=${REDIS_DATA_VOLUME:-"${REDIS_CONTAINER_NAME}-data"}
REDIS_DATA_DIR=${REDIS_DATA_DIR:-"${THIS_SCRIPT_DIR}/redis-stack-server-data"}
REDIS_VOLUME_LABEL=${REDIS_VOLUME_LABEL:-Z}
REDIS_ARGS=${REDIS_ARGS:-}
REDIS_ARGS_MODE=${REDIS_ARGS_MODE:-env}

if ! command -v "${CONTAINER_RUNTIME}" >/dev/null 2>&1; then
    echo "Container runtime not found: ${CONTAINER_RUNTIME}" >&2
    exit 127
fi

if "${CONTAINER_RUNTIME}" container inspect "${REDIS_CONTAINER_NAME}" >/dev/null 2>&1; then
    if [ "${REDIS_CONTAINER_REPLACE}" = "1" ]; then
        "${CONTAINER_RUNTIME}" rm -f "${REDIS_CONTAINER_NAME}" >/dev/null
    else
        echo "Container already exists: ${REDIS_CONTAINER_NAME}" >&2
        echo "Set REDIS_CONTAINER_REPLACE=1 to remove it before starting." >&2
        exit 1
    fi
fi

case "${REDIS_VOLUME_MODE}" in
    volume)
        REDIS_VOLUME_SPEC="${REDIS_DATA_VOLUME}:/data"
        ;;
    bind)
        mkdir -p "${REDIS_DATA_DIR}"

        REDIS_VOLUME_SPEC="${REDIS_DATA_DIR}:/data"
        if [ -n "${REDIS_VOLUME_LABEL}" ]; then
            REDIS_VOLUME_SPEC="${REDIS_VOLUME_SPEC}:${REDIS_VOLUME_LABEL}"
        fi
        ;;
    *)
        echo "Unsupported REDIS_VOLUME_MODE: ${REDIS_VOLUME_MODE}" >&2
        echo "Use 'volume' or 'bind'." >&2
        exit 1
        ;;
esac

case "${REDIS_ARGS_MODE}" in
    env|argv)
        ;;
    *)
        echo "Unsupported REDIS_ARGS_MODE: ${REDIS_ARGS_MODE}" >&2
        echo "Use 'env' for Redis Stack images or 'argv' for official Redis images." >&2
        exit 1
        ;;
esac

if [ "${REDIS_ARGS_MODE}" = "argv" ]; then
    # The official Redis image accepts redis-server options as command
    # arguments. Redis Stack images use the REDIS_ARGS environment variable.
    # shellcheck disable=SC2086
    set -- ${REDIS_ARGS}

    # shellcheck disable=SC2086
    exec "${CONTAINER_RUNTIME}" run ${REDIS_CONTAINER_RUN_FLAGS} \
        --name "${REDIS_CONTAINER_NAME}" \
        -p "${REDIS_PORT}:6379" \
        -v "${REDIS_VOLUME_SPEC}" \
        "${REDIS_IMAGE}" "$@"
fi

# shellcheck disable=SC2086
exec "${CONTAINER_RUNTIME}" run ${REDIS_CONTAINER_RUN_FLAGS} \
    --name "${REDIS_CONTAINER_NAME}" \
    -p "${REDIS_PORT}:6379" \
    -v "${REDIS_VOLUME_SPEC}" \
    -e "REDIS_ARGS=${REDIS_ARGS}" \
    "${REDIS_IMAGE}"
