## Install external packages

### Prerequisites for AlmaLinux 9 and 10

```bash
dnf -y update && \
dnf -y install \
    epel-release \
    dnf-plugins-core && \
dnf config-manager --set-enabled crb && \
dnf -y groupinstall "Development Tools" && \
dnf -y install \
    bash-completion \
    gcc \
    gcc-c++ \
    cmake \
    make \
    ninja-build \
    mold \
    git \
    autoconf \
    libtool \
    libcurl-devel \
    openssl-devel \
    gnutls-devel \
    zlib-devel \
    bzip2-devel \
    libzstd-devel \
    libquadmath-devel \
    python3-devel

# If needed for AlaLinux 9
# dnf gcc-toolset-14
```

### Build and install external dependencies
The following command installs ZeroMQ, boost, FairLogger, FairMQ, hiredis, and redis++.

```bash
# download the source code
git clone https://github.com/spadi-alliance/nestdaq

# configure
cmake \
  -DCMAKE_INSTALL_PREFIX=./install \
  -DBUILD_PARALLEL_LEVEL=$(nproc) \
  -B ./build-external \
  -S nestdaq/cmake

# Both the build and install steps are executed
cmake --build ./build-external
```

- In the command example above, CMake’s `ExternalProject` is used to perform `git clone`, build, and installation.
  - In this case, the `--parallel` (or `-j`) option cannot be used with `cmake --build`,
    so please specify the parallel build level during the initial configuration using `-DBUILD_PARALLEL_LEVEL=xxx`.
      - The `nproc` command prints the number of CPU cores available on the system. If building with this value causes resource shortages, please specify the value manually.
  - The default versions of the external dependency libraries that are installed are summarized in the section below. To modify the library versions, add `-Dxxxx_VERSION=yyyy` to the CMake options. 
- If `-DWITH_OTEL_CPP=ON` is specified, it also installs opentelemetry-cpp (along with its dependencies such as nlohmann/json, gRPC, etc.). (Default: `WITH_OTEL_CPP=OFF`)
- To use `ninja` instead of `make`, add `-G Ninja` to the CMake options
- To use `mold` instead of the system `ld`, 
  - GCC 12.1 or later: Add `-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"` and `-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=mold"` to the CMake options
  - GCC 12.0 or earlier: Add `-DCMAKE_EXE_LINKER_FLAGS="-B<path-to-mold>"` and `-DCMAKE_SHARED_LINKER_FLAGS="-B<path-to-mold>"`

#### Versions of installed external depdendencies

| Packages                                                                 | Version (default) | CMake options to modify versions |
| :--                                                                      | :--               | :--                              |
| [ZeroMQ(libzmq)](https://github.com/zeromq/libzmq)                       | 4.3.5             | `ZeroMQ_VERSION`                 |
| [boost](https://github.com/boostorg/boost)                               | 1.85.0            | `Boost_VERSION`                  | 
| [FairLogger](https://github.com/FairRootGroup/FairLogger)                | 2.3.0             | `FairLogger_VERSION`             |
| [FairMQ](https://github.com/FairRootGroup/FairMQ)                        | 1.10.0            | `FairMQ_VERSION`                 |
| [hiredis](https://github.com/redis/hiredis)                              | 1.3.0             | `hiredis_VERSION`                |
| [redis++](https://github.com/sewenew/redis-plus-plus)                    | 1.3.15            | `redis_plus_plus_VERSION`        |
| [opentelemetry-cpp](https://github.com/open-telemetry/opentelemetry-cpp) | 1.24.0            | `opentelemetry-cpp_VERSION`      |

### Build and install NestDAQ library
```bash
cmake \
  -DCMAKE_PREFIX_PATH=./install \
  -DCMAKE_INSTALL_PREFIX=./install \
  -B ./build \
  -S nestdaq
cmake --build ./build --parallel $(nproc)
cmake --install ./build
```

- In the example above, both the main nestdaq package and the external dependency libraries are installed in the same directory (`./install`).
  If the external dependencies are installed in a different location, please specify the directory where the they were installed using `-DCMAKE_PREFIX_PATH=xxx`.