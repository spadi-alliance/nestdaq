# Examples

Set of examples of how to use the plugins.
The scripts can be copied to your favorite directory. 
Redis server must be started before executing the scripts. 

## start_device.sh
This example shows how to start FairMQDevice with the custom plugins. 
The device must be those provided by the present repository or those which contains `fairmq-` in the path. 

```bash
  # ./start_device.sh [device-name] [options ...]
  ./start_device.sh Sampler
```

```bash
  ./start_device.sh /your-fairmq-install-path/bin/fairmq-splitter
```

## topology-1-1.sh
A simple topology of **Sampler** and **Sink** with the **PUSH-PULL** pattern. 
If _N_ Sasmplers and _N_ Sinks are started, they forms _N_ pairs of Sampler and Sink.  
Each Sampler sends data to one Sink with the same instance index. 

```bash
  ./topology-1-1.sh
```

```mermaid
graph LR
  Sampler-0 --> Sink-0
  Sampler-1 --> Sink-1
  Sampler-2 --> Sink-2
```

## topology-n-n-m.sh
A simple topology of _N_-**Sampler**s, _N_-**fairmq-splitter**s, and _M_-**Sink**s with the **PUSH-PULL** pattern. 
Each Sampler sends data to one fairmq-splitter with the same instance index. 
Then, the fairmq-splitter sends the data to Sinks. 
The `autoSubChannel true` flag is used to give each sub-socket a different `address:port` and to distinguish them by index.
The fairmq-splitter determines the destination by the number of messages sent in a round-robin fashion.

```bash
  ./topology-n-n-m.sh
```

```mermaid
graph LR
  Sampler-0 --> fairmq-splitter-0
  Sampler-1 --> fairmq-splitter-1
  Sampler-2 --> fairmq-splitter-2
  fairmq-splitter-0 & fairmq-splitter-1 & fairmq-splitter-2  --> Sink-0 & Sink-1
```

## mq-param.sh
This example shows how to configure parameters via Redis. 

```bash
  ./mq-param.sh
```
