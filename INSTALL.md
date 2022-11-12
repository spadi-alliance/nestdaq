## Install external packages
### Install Redis (server and command line interface)
(build example)
```bash
  git clone https://github.com/redis/redis.git
  cd redis
  git checkout -b 6.0.10 6.0.10
  
  # note: default install path is "/usr/local/bin"
  make 
  make install

  # to change the install path, set a variable "PREFIX"
  make PREFIX=/your/favorite/path
  make PREFIX=/your/favorite/path install

  # or just build and copy the binaries of (redis-benchmark, redis-check-aof, redis-cli, redis-check-rdb, redis-sentinel, redis-server) 
  make
  cd src 
  cp redis-server redis-client /your/favorite/path
```

### Install Redis TimeSeries (extension module for times series data)
(build example)
```bash
  git clone --recursive https://github.com/RedisTimeSeries/RedisTimeSeries.git
  cd RedisTimeSeries
  git checkout -b v1.4.8 v1.4.8
  make
  # copy the library or link to the library
  cp bin/linux-x64-release/redistimeseries.so /your/favorite/path
```

### Install Grafana

(TODO)

### (optional) Install RedisInsight (Redis GUI client) 
- Windows10 https://downloads.redisinsight.redislabs.com/latest/redisinsight-win.msi
- Mac       https://donwloads.redisinsight.redislabs.com/latest/redisinsight-mac.dmg
- Linux     https://downloads.redisinsight.redislabs.com/latest/redisinsight-linux64
```bash
  curl -O https://downloads.redisinsight.redislabs.com/latest/redisinsight-linux64
  chmod +x redisinsight-linux64
```

## Install prerequisites

(TODO)
### Install FairMQ

  `N` in option `-jN` is the number of parallel jobs.  
   The installation directory can be configured by `--prefix=` or `-DCMAKE_INSTALL_REFIX=` option.  
- Install boost  
  The following example shows how to build boost 1.79.0.  
  ```bash
  git clone https://github.com/boostorg/boost.git
  cd boost
  git checkout -b boost-1.79.0 boost-1.79.0
  git submodule update --init --recursive
  ./bootstrap.sh 
  
  ./b2 install \
   link=static,shared \
   threading=single,multi \
   cxxstd=17 \
   variant=release,debug \
   --layout=tagged \
   -jN \
   --prefix=/your/favorite/path
  ```
- Install zeromq   
  The following example shows how to build zeromq v4.3.4.   
  ```bash
  git clone https://github.com/zeromq/libzmq.git
  cd libzmq
  git checkout -b v4.3.4 v4.3.4 
  cd ../
  cmake \
   -DCMAKE_INSTALL_PREFIX=/your/favorite/path \
   -DCMAKE_CXX_STANDARD=17 \
   -B build \
   -S libzmq
  cd build
  make -jN
  make install
  ```
- (optional) Install fmtlib  
  The following example shows how to build fmtlib version 9.0.0.   
  ```bash
  git clone https://github.com/fmtlib/fmt.git
  cd fmt
  git checkout -b 9.0.0 9.0.0
  cd ../
  cmake \
   -DCMAKE_INSTALL_PREFIX=/your/favorite/path \
   -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE \
   -DCMAKE_CXX_STANDARD=17 \
   -B build \
   -S fmt
  cd build
  make -jN
  make install
  ```
- Install FairLogger  
  The following example shows how to build FairLogger v1.11.0. 
  ```bash
    git clone https://github.com/FairRootGroup/FairLogger.git
    cd FairLogger
    git checkout -b v1.11.0 v1.11.0
    cd ../
    
    cmake \
     -DCMAKE_INSTALL_PREFIX=/your/favorite/path \
     -DCMAKE_CXX_STANDARD=17 \
     -DUSE_EXTERNAL_FMT=OFF \
     -B build \
     -S FairLogger
    
     # If fmtlib bundled with FairLogger is used, fmt_PREFIX is not needed. 
     fmt_PREFIX=/fmt-install-dir && \
     cmake \
      -DCMAKE_INSTALL_PREFIX=/your/favorite/path \
      -DCMAKE_CXX_STANDARD=17 \
      -DUSE_EXTERNAL_FMT=ON \
      -DCMAKE_PREFIX_PATH="$fmt_PREFIX;" \
      -B build \
      -S FairLogger
     
     cd build
     make -jN
     make install
     
  ```
- Install FairMQ
  The following example shows how to build FairMQ v1.4.52. 
  ```bash
    git clone https://github.com/FairRootGroup/FairMQ.git
     cd FairMQ
     git checkout -b v1.4.52 v1.4.52
     cd ../
    
     # If fmtlib bundled with FairLogger is used, fmt_PREFIX is not needed. 
    fmt_PREFIX=/fmt-install-dir \
    FairLogger_PREFIX=/fairlogger-install-dir \
    boost_PREFIX=/boost-install-dir \
    libzmq_PREFIX=/zeromq-install-dir && \
    cmake \
     -DCMAKE_INSTALL_PREFIX=/your/favorite/path \
     -DCMAKE_CXX_STANDARD=17 \
    	-DCMAKE_PREFIX_PATH="$FairLogger_PREFIX;$boost_PREFIX;$libzmq_PREFIX;$fmt_PREFIX;" \
    	-B build \
    	-S FairMQ 
    cd build
    make -jN
    make install
  ```


## Install Redis client libraries (hiredis and redis-plus-plus)
(build example for hiredis)
```bash
  git clone https://github.com/redis/hiredis.git
  cd hiredis
  git checkout -b v1.0.0 v1.0.0
  
  # note: default install path is "/usr/local"
  make
  make install

  # to change the install path, set a variable "PREFIX"
  make PREFIX=/your/favorite/path
  make PREFIX=/your/favorite/path install
```

(build exmaple for redis-plus-plus)
```bash
  git clone https://github.com/sewenew/redis-plus-plus.git
  cd redis-plus-plus

  # to use RedLock (distributed lock algorithm), checkout "1.3.6" or later
  git checkout -b 1.3.6 1.3.6
  cd ..

  # If "hiredis" is installed at non-default location, 
  # you should use `CMAKE_PREFIX_PATH` to specify the installation path of "hiredis".
  #
  # c++17 option is not "CMAKE_CXX_STANDARD=17" but "DREDIS_PLUS_PLUS_CXX_STANDARD=17". 
  cmake \
    -DCMAKE_INSTALL_PREFIX=/your/favorite/path \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$hiredisInstallPath \ 
    -DREDIS_PLUS_PLUS_CXX_STANDARD=17 \
    -DREDIS_PLUS_PLUS_BUILD_TEST=OFF \
    -B ./build \
    -S ./redis-plus-plus
  cd build
  make install
```

## Install nestdaq
### Build example
```bash
  git clone https://github.com/spadi-alliance/nestdaq.git

  cmake \
    -DCMAKE_INSTALL_PREFIX=./install \
    -DCMAKE_PREFIX_PATH="$FairSoftInstallPath;$RootInstallPath;$hiredisInstallPath;$redis_plus_plus_InstallPath" \
    -B ./build \
    -S ./nestdaq
  cd build
  make install
```
