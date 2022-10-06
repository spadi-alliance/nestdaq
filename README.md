# NestDAQ
A streaming DAQ implementation for the particle measurements


## Tested system
| System | Version | Compiler                 | CMake                          |
| ---    | ---     | ---                      | ---                            | 
| CentOS | 7       | GCC 8.3.1 (devtoolset-8) | 3.14.6 or later (epel: cmake3) |

## External packages used with NestDAQ
| Packages         | Version  | URL |
| ---              | ---      | --- |
| Redis            | 6.0.10   | https://github.com/redis/redis/ |
| Redis TimeSeries | 1.4.18   | https://github.com/RedisTimeSeries/RedisTimeSeries/ |
| Grafana          |          | | 

## Dependencies to build NestDAQ

| Packages         | Version                      | URL |
| ---              | ---                          | --- |
| boost            | 1.72.0 or later              | |
| FairLogger       | 1.9.0  or later              | |
| FairMQ           | 1.4.26 or later              | |
| hiredis          | 1.0.0                        | https://github.com/redis/hiredis/ |
| redis-plus-plus  | 1.2.1 <br> (recipes branch)  | https://github.com/sewenew/redis-plus-plus|


## [Installation](INSTALL.md)
