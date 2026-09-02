if(NOT DEFINED REDISJSON_SOURCE_DIR)
  message(FATAL_ERROR "REDISJSON_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE redisjson_cargo_tomls
  "${REDISJSON_SOURCE_DIR}/Cargo.toml"
  "${REDISJSON_SOURCE_DIR}/*/Cargo.toml"
)

# Cargo accepts "default_features" today but warns that the underscore form will
# stop working in the 2024 edition. Patch RedisJSON manifests before invoking
# the upstream Makefile.
foreach(cargo_toml IN LISTS redisjson_cargo_tomls)
  file(READ "${cargo_toml}" content)
  string(REPLACE "default_features" "default-features" content "${content}")
  file(WRITE "${cargo_toml}" "${content}")
endforeach()

# RedisJSON carries helper scripts that install Rust into $HOME and append
# "source $HOME/.cargo/env" to ~/.bashrc. The Redis Stack wrapper already
# provides an isolated Rust toolchain via CARGO_HOME/RUSTUP_HOME, so replace
# those helpers in the prepared RedisJSON tree before any upstream setup target
# can invoke them.
set(redisjson_temp_rust_shim [=[
#!/bin/sh
set -eu

if ! command -v cargo >/dev/null 2>&1; then
  echo "cargo is required in PATH; Redis Stack should provide a temporary Rust toolchain" >&2
  exit 1
fi

if ! command -v rustc >/dev/null 2>&1; then
  echo "rustc is required in PATH; Redis Stack should provide a temporary Rust toolchain" >&2
  exit 1
fi

cargo --version
rustc --version
]=])

foreach(redisjson_getrust IN ITEMS
    "${REDISJSON_SOURCE_DIR}/.install/getrust.sh"
    "${REDISJSON_SOURCE_DIR}/deps/readies/bin/getrust")
  if(EXISTS "${redisjson_getrust}")
    file(WRITE "${redisjson_getrust}" "${redisjson_temp_rust_shim}")
    file(CHMOD "${redisjson_getrust}"
      PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
  endif()
endforeach()
