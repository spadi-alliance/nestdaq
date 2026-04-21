# NestDAQ
A streaming DAQ implementation for the particle measurements


## Tested system
| System    | Version | Compiler                     | CMake           |
| ---       | ---     | ---                          | ---             | 
| AlmaLinux | 9       | GCC 11.5.0                   | 3.26.5 or later |
| AlmaLinux | 9       | GCC 14.2.1 (gcc-toolset-14)  | 3.26.5 or later |
| AlmaLinux | 10      | GCC 14.2.1                   | 3.30.5 or later |

## Dependencies to build NestDAQ

| Packages         | Version                              | URL |
| ---              | ---                                  | --- |
| Boost            | 1.72.0 or later                      | |
| FairLogger       | 1.9.0  or later                      | |
| FairMQ           | 1.4.26 or later                      | |
| hiredis          | 1.0.0  or later                      | https://github.com/redis/hiredis/ |
| redis-plus-plus  | 1.2.1 <br> (recipes branch) or later | https://github.com/sewenew/redis-plus-plus|


## [Installation](INSTALL.md)
