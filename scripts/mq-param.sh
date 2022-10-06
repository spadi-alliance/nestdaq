#!/bin/bash

server=redis://127.0.0.1:6379/2

function param () {
  # "instance":"field" "value"
  #echo redis-cli -u $server set parameters:$1:$2 ${@:3}
  #redis-cli -u $server set parameters:$1:$2 ${@:3}
  echo redis-cli -u $server hset parameters:$1 ${@:2}
  redis-cli -u $server hset parameters:$1 ${@:2}
}

#==============================================================================
#      isntance-id       field  value    field value   field          value
param  Sampler-0         text   Hello    rate  2       max-iterations 0
param  Sampler-1         text   world    rate  2       max-iterations 0
param  Sampler-2         text   hoge     rate  2       max-iterations 0
param  Sampler-3         text   piyo     rate  2       max-iterations 0

#param Sink-0 multipart false
#param Sink-1 multipart false
#param Sink-2 multipart false
#param Sink-3 multipart false

param Sink-0 multipart true
param Sink-1 multipart true 
param Sink-2 multipart true 
param Sink-3 multipart true 
