# Example for web-based DAQ controller GUI

## daq-webctl.html
A simple html (and JavaScript) used by `daq-webctl`. 
`daq-webctl` uses http and websocket implementation of `Boost.Beast`. 

```bash
  # The following command shows command options
  /your-install-path/bin/daq-webctl --help

  # Redis server must be started before starting webgui-ws. Then, 
  /your-install/path/bin/daq-webctl

```

After starting `daq-webctl`, open the URL `http://localhost:8080/daq-webctl.html` in a web browser.   

Note:
- Run number must be set before entering to the Running state. 
