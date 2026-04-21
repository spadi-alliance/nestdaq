if(NOT DEFINED REDISJSON_SOURCE_DIR)
  message(FATAL_ERROR "REDISJSON_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE redisjson_cargo_tomls
  "${REDISJSON_SOURCE_DIR}/Cargo.toml"
  "${REDISJSON_SOURCE_DIR}/*/Cargo.toml"
)

# Cargo accepts "default_features" today but warns that the underscore form will
# stop working in the 2024 edition. Patch vendored RedisJSON manifests in the
# copied Redis module tree without modifying the FetchContent source checkout.
foreach(cargo_toml IN LISTS redisjson_cargo_tomls)
  file(READ "${cargo_toml}" content)
  string(REPLACE "default_features" "default-features" content "${content}")
  file(WRITE "${cargo_toml}" "${content}")
endforeach()
