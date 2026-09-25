# Redis Stack Container Helpers

[English](README.md) | [日本語](README.ja.md)

[Top: NestDAQ](../../README.md) | [Previous: Telemetry](../../nestdaq/telemetry/README.md) | [Next: OpenTelemetry Collector container setups](../otel-collector-compose/README.md)

This directory contains helper scripts for starting [Redis Stack](../../INSTALL.md#external-runtime-components) containers for local NestDAQ validation.
The containers publish ports on the host and do not enable Redis authentication by default.
If connected to a public or shared network as-is, anyone who can reach the Redis port might read, modify, or delete its data.
RedisInsight-enabled images also publish the port for its web UI.
Use these helpers only in a trusted local environment.

For development and local inspection, the Redis Stack image includes RedisInsight for viewing and operating on data.
For production deployments, prefer the Redis Stack Server image without RedisInsight.
The server-only image avoids running an unnecessary web UI, published port, process, and persistent state in the same container as the production Redis server.
This image choice alone does not secure a production deployment; configure authentication, Transport Layer Security (TLS), network access controls, persistence, and backups separately.
See the official Redis documentation on [running Redis Stack with Docker](https://redis.io/docs/latest/operate/oss_and_stack/install/archive/install-stack/docker/) for the intended use of each image.

The scripts use pinned image tags instead of `latest`:

- `docker.io/redis/redis-stack:7.4.0-v8` for development Redis Stack with RedisInsight.
- `docker.io/redis/redis-stack-server:7.4.0-v8` for production-oriented Redis Stack Server only.
- `docker.io/library/redis:8.2.7` for the official Redis 8.2.7 image.
- `docker.io/redis/redis-stack:7.2.0-v20` for Redis Stack 7.2 with RedisInsight.
- `docker.io/redis/redis-stack-server:7.2.0-v20` for Redis Stack 7.2 Server only.

## 1. Choose an Image

The available helper scripts and their images are listed in [Table 1](#tbl-helper-script-images-en).

<a id="tbl-helper-script-images-en"></a>
**Table 1: Redis container helper scripts and images.**

| Script | Image | RedisInsight | Notes |
| :-- | :-- | :-- | :-- |
| `run-redis-8.2.7.sh` | `docker.io/library/redis:8.2.7` | no | Official Redis image. The Redis 8 package is expected to include Redis Stack modules; verify with `MODULE LIST` after startup. |
| `run-redis-7.2-stack.sh` | `docker.io/redis/redis-stack:7.2.0-v20` | yes | Redis Stack 7.2 image line for development and local inspection. |
| `run-redis-7.2-stack-server.sh` | `docker.io/redis/redis-stack-server:7.2.0-v20` | no | Redis Stack 7.2 server-only image line. |
| `run-redis-stack.sh` | `docker.io/redis/redis-stack:7.4.0-v8` | yes | Default Redis Stack development helper. |
| `run-redis-stack-server.sh` | `docker.io/redis/redis-stack-server:7.4.0-v8` | no | Default Redis Stack server-only helper. |

Check the running Redis version and loaded modules with:
In shell command examples, lines beginning with `#` are comments for the reader and are not executed by the shell.

```sh
# Report the running Redis server version.
redis-cli -p 6379 INFO server
# List the modules loaded by the Redis server.
redis-cli -p 6379 MODULE LIST
```

The Redis Stack 7.2 image tags identify Stack releases, not exact Redis server patch versions.
When the exact Redis server patch version matters, verify it with the commands above after startup.

## 2. Start Redis 8.2.7

Run:

```sh
# Start the pinned Redis 8.2.7 container.
./run-redis-8.2.7.sh
```

Default endpoint:

- Redis: `localhost:6379`

The script bind-mounts `redis-8.2.7-data/`, located next to the script, at `/data/` in the container.
Because this helper uses the official Redis image, it passes additional Redis server arguments from `REDIS_ARGS` as container command arguments.

## 3. Start Redis Stack 7.2

Run Redis Stack with RedisInsight:

```sh
# Start Redis Stack 7.2 with RedisInsight.
./run-redis-7.2-stack.sh
```

Run Redis Stack Server only:

```sh
# Start the server-only Redis Stack 7.2 container.
./run-redis-7.2-stack-server.sh
```

Default endpoints:

- Redis: `localhost:6379`
- RedisInsight: `http://localhost:8001` when using `run-redis-7.2-stack.sh`

## 4. Start Redis Stack with RedisInsight

Run:

```sh
# Start the default Redis Stack container with RedisInsight.
./run-redis-stack.sh
```

Default endpoints:

- Redis: `localhost:6379`
- RedisInsight: `http://localhost:8001`

The script bind-mounts `redis-stack-data/`, located next to the script, at `/data/` in the container.
It also bind-mounts `redisinsight-data/` at `/redisinsight/`, allowing RedisInsight to create its internal subdirectories within that directory.

## 5. Start Redis Stack Server Only

Run:

```sh
# Start the default server-only Redis Stack container.
./run-redis-stack-server.sh
```

Default endpoint:

- Redis: `localhost:6379`

The script bind-mounts `redis-stack-server-data/`, located next to the script, at `/data/` in the container.

## 6. Rerun Behavior

By default, each script removes an existing container with the configured name before starting a new container.
This behavior allows the script to run again after an interrupted invocation or when a same-name container remains.
Persistent Redis data remains in the configured bind-mounted data directory or named volume.

Set `REDIS_CONTAINER_REPLACE=0` to make the script fail when a same-name container already exists.

## 7. Security-Enhanced Linux (SELinux)

SELinux label options apply only when `REDIS_VOLUME_MODE=bind`.
By default, bind mounts use the `:Z` label option so that the container can write to the data directory on an SELinux-enabled host.
Set `REDIS_VOLUME_LABEL=z` when multiple containers must share the same data directory.
Set `REDIS_VOLUME_LABEL=` to omit the label option.
RedisInsight-enabled helpers apply the same label option to both the Redis and RedisInsight bind mounts.

## 8. Directory Permissions

By default, the scripts create bind-mounted data directories as the host user running the script and do not change their permissions.
On rootless Podman, container root normally maps to the host user running the container.
The created directories are therefore usually writable without additional permission changes.

SELinux labeling and Unix permissions are independent controls.
The `:Z` mount label allows the container to access the directory on an SELinux-enabled host, but it does not resolve user identifier or group identifier (uid/gid) permission mismatches.
Rootful containers may create files owned by host root in bind-mounted directories.
If a bind-mounted directory is not writable, adjust its host-side ownership or permissions outside these helper scripts.

## 9. Named Volumes

Named volumes are optional.
Use `REDIS_VOLUME_MODE=volume` to have Docker or Podman manage Redis data outside the helper script directory.

Inspect volumes with:

```sh
# List volumes managed by Docker.
docker volume ls
# List volumes managed by Podman.
podman volume ls
```

Remove named volumes when you want to discard local Redis data:

```sh
# Remove all Docker volumes created by these Redis helpers.
docker volume rm nestdaq-redis-stack-data nestdaq-redis-stack-redisinsight
docker volume rm nestdaq-redis-stack-server-data
docker volume rm nestdaq-redis-8.2.7-data
docker volume rm nestdaq-redis-7.2-stack-data nestdaq-redis-7.2-stack-redisinsight
docker volume rm nestdaq-redis-7.2-stack-server-data
```

or:

```sh
# Remove all Podman volumes created by these Redis helpers.
podman volume rm nestdaq-redis-stack-data nestdaq-redis-stack-redisinsight
podman volume rm nestdaq-redis-stack-server-data
podman volume rm nestdaq-redis-8.2.7-data
podman volume rm nestdaq-redis-7.2-stack-data nestdaq-redis-7.2-stack-redisinsight
podman volume rm nestdaq-redis-7.2-stack-server-data
```

Use named volumes when Redis data directories should not be stored next to the helper scripts:

```sh
# Store Redis data in a runtime-managed named volume.
REDIS_VOLUME_MODE=volume ./run-redis-stack.sh
```

<a id="10-runtime-options"></a>
## 10. Environment Variables

Each script uses its containing directory as `THIS_SCRIPT_DIR`.
Bind-mounted data directories are relative to `THIS_SCRIPT_DIR`, so a copied installed script stores its data next to the copied script.

Runtime environment variables are described in [Table 2](#tbl-runtime-environment-variables-en).

<a id="tbl-runtime-environment-variables-en"></a>
**Table 2: Redis container runtime environment variables.**

| Variable | Default | Description |
| -------- | ------- | ----------- |
| `CONTAINER_RUNTIME` | `docker` | Container engine command. Set to `podman` to use Podman. |
| `REDIS_CONTAINER_NAME` | Script-specific name | Container name. |
| `REDIS_CONTAINER_REPLACE` | `1` | Remove an existing same-name container before starting. Set to `0` to fail instead. |
| `REDIS_IMAGE` | Script-specific pinned image | Container image. |
| `REDIS_PORT` | `6379` | Host port mapped to Redis port `6379`. |
| `REDIS_INSIGHT_PORT` | `8001` | Host port mapped to RedisInsight port `8001`; used only by RedisInsight-enabled helpers. |
| `REDIS_CONTAINER_RUN_FLAGS` | `--rm -it` | Flags passed to `docker run` or `podman run`. Use `-d --rm` for non-interactive validation. |
| `REDIS_VOLUME_MODE` | `bind` | Storage mode. Use `bind` for host bind mounts or `volume` for named volumes. |
| `REDIS_DATA_VOLUME` | Container-name-based volume | Named volume mounted to `/data/`; used only in `volume` mode. |
| `REDIS_INSIGHT_VOLUME` | Container-name-based volume | Named volume mounted to `/redisinsight/`; used only by RedisInsight-enabled helpers in `volume` mode. |
| `REDIS_DATA_DIR` | Data directory next to the script | Host directory bind-mounted to `/data/`; used only in `bind` mode. |
| `REDIS_INSIGHT_DATA_DIR` | Script-specific RedisInsight data directory | Host directory bind-mounted to `/redisinsight/`; used only by RedisInsight-enabled helpers in `bind` mode. |
| `REDIS_VOLUME_LABEL` | `Z` | SELinux bind-mount label option; used only in `bind` mode. Use `z` for shared labeling or an empty value to disable. |
| `REDIS_ARGS` | empty string (`""`) | Extra Redis server arguments. Redis Stack images receive this through the image `REDIS_ARGS` environment variable; the official Redis 8.2.7 helper passes it as command arguments. |
| `REDIS_ARGS_MODE` | `env` or `argv` | Argument passing mode used by `run-redis-stack-server.sh`. Use `env` for Redis Stack images and `argv` for official Redis images. |

Example:

```sh
# Start Redis Stack on custom ports with password authentication.
REDIS_PORT=16379 \
REDIS_INSIGHT_PORT=18001 \
REDIS_ARGS="--requirepass nestdaq" \
./run-redis-stack.sh
```

For Podman:

```sh
# Start the server-only helper with Podman instead of Docker.
CONTAINER_RUNTIME=podman ./run-redis-stack-server.sh
```

Stop the foreground container with Ctrl-C.
The container is removed on exit, but the bind-mounted data directory or named volume is retained.
