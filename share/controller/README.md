# Web Controller Browser Files

[English](README.md) | [日本語](README.ja.md)

[Top: NestDAQ](../../README.md) | [Previous: Web controller](../../controller/README.md) | [Next: Telemetry](../../nestdaq/telemetry/README.md)

This directory contains the browser files installed for `daq-webctl`, the NestDAQ web controller.

## 1. `daq-webctl.html`

`daq-webctl.html` is the default browser graphical user interface (GUI) served by `daq-webctl`.
The install process places this file under the controller document root as `daq-webctl.html`.

The install process also creates `index.html` as a symbolic link to this file.
The user interface (UI) is therefore available at either `/daq-webctl.html` or `/`.

See [`controller/README.md`](../../controller/README.md) for the controller implementation, startup commands, Redis requirements, command-line options, behavior after startup, and browser usage notes.
