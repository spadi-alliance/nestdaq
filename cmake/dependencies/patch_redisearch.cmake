if(NOT DEFINED REDISEARCH_SOURCE_DIR)
  message(FATAL_ERROR "REDISEARCH_SOURCE_DIR is required")
endif()
if(NOT DEFINED REDISEARCH_MODULE_MAKEFILE)
  message(FATAL_ERROR "REDISEARCH_MODULE_MAKEFILE is required")
endif()

set(redisearch_cmakelists "${REDISEARCH_SOURCE_DIR}/CMakeLists.txt")
set(redisearch_source_makefile "${REDISEARCH_SOURCE_DIR}/Makefile")
set(redisearch_boost_cmake "${REDISEARCH_SOURCE_DIR}/build/boost/boost.cmake")

# Redis' DISABLE_WERRORS handling removes "-Werror" but leaves the suffix of
# flags such as "-Werror=implicit-function-declaration". Replace those flags
# with warning-only variants before Redis' module Makefile sees them.
file(READ "${redisearch_cmakelists}" content)
string(REPLACE
  " -Werror=incompatible-pointer-types -Werror=implicit-function-declaration"
  " -Wno-incompatible-pointer-types -Wno-implicit-function-declaration"
  content
  "${content}"
)
file(WRITE "${redisearch_cmakelists}" "${content}")

# RediSearch only needs header-oriented Boost components here. Limiting
# BOOST_INCLUDE_LIBRARIES avoids building unrelated Boost libraries and reduces
# noisy warnings from unused Boost targets.
file(READ "${redisearch_boost_cmake}" content)
string(REPLACE
  "# set(BOOST_INCLUDE_LIBRARIES boost geometry optional unordered)"
  "set(BOOST_INCLUDE_LIBRARIES headers geometry optional unordered smart_ptr uuid)"
  content
  "${content}"
)
# DOWNLOAD_EXTRACT_TIMESTAMP was added to ExternalProject_Add in CMake 3.24.
# RediSearch's Boost FetchContent forwards it to older CMake releases as part
# of the URL list, which breaks Ubuntu 22.04's CMake 3.22.
string(REPLACE
  "            DOWNLOAD_EXTRACT_TIMESTAMP TRUE\n"
  ""
  content
  "${content}"
)
file(WRITE "${redisearch_boost_cmake}" "${content}")

# RediSearch v8.6.x writes the module under search-community, while the Redis
# module wrapper target still points at search/redisearch.so.
file(READ "${REDISEARCH_MODULE_MAKEFILE}" content)
string(REPLACE
  "/search/redisearch.so"
  "/search-community/redisearch.so"
  content
  "${content}"
)
file(WRITE "${REDISEARCH_MODULE_MAKEFILE}" "${content}")

# RediSearch's distro dependency checker does not recognize AlmaLinux. The
# wrapper already provides the required build inputs, so skip this preflight
# instead of printing an expected failure on every build.
file(READ "${redisearch_source_makefile}" content)
string(REPLACE
  "build: \$(BUILD_SCRIPT) verify-deps"
  "build: \$(BUILD_SCRIPT)"
  content
  "${content}"
)
file(WRITE "${redisearch_source_makefile}" "${content}")
