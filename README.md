[![Clang-Tidy](https://github.com/Arm-Debug/ASTL/actions/workflows/integration.yml/badge.svg)](https://github.com/Arm-Debug/ASTL/actions/workflows/integration.yml)
# ATL
Arm Telemetry Library

# Description
ATL is a self-contained library for platform telemetry collection at Arm. It abstracts low level interfaces to telemetry data sources on the system. Using a predefined API, a telemetry collection tool or an AI framework can dynamically discover available supported telemetry on the target platform, configure, start, (pause, resume), stop a collection and process collected data. Collected data can be streamed directly to a user provided buffer or can be written to a specified output file format, such as perfeto json file for data visualization.

The initial implementation focuses on the System Control and Management Interface (SCMI) specification through the Linux SCMI sysfs interface. It may eventually be expanded to add support for other interfaces such as: BIOS mailboxes, PCIe configuration spaces, direct register accesses, MMIO, OS provided data or other sources of data.

The library has a C-interface for the API and a C++ implementation. There is also plan to offer a python wrapper interface (not implemented yet)

# Key Goals and Properties
## Sharable
- New or other tools at Arm can use it (not used yet)
- Partners and external 3rd party tool developers can use it to access telemetry on Arm platforms (not used yet)
## Uniform
- Telemetry collection through fixed predefined set of API (not defined yet)
## Portable
- Rebuild on Windows or other OS’s with same user API interface (not ported yet)
- Wrap with python layer (not done yet)
## Extensible:
- Driver to driver context-switch based collection (not implemented yet)
- SCMI specification extensions (not implemented yet)
- New/other platform level telemetry access mechanisms (not implemented yet)
## Reusable
- Can be deployed on all new platforms: IOT, Automotive, Client, Data center, GPUs, NPUs. (not deployed yet)
- Can be used by a telemetry collection tool or in an AI framework or directly to instrument a workload (not used yet)

# High Level Architecture Diagram
![image](https://github.com/user-attachments/assets/e0005e46-ac89-49f6-95db-b361d14d4aab)

# Telemetry Collection Tool Usage Example Diagram
![image](https://github.com/user-attachments/assets/ee543a10-fae6-45a8-8305-7cce78a3521b)

# AI Framework Usage Example Diagram
![image](https://github.com/user-attachments/assets/39fc861b-172b-4c51-85d1-a5c6eb86b5b7)

# Testing and Isolation Methodology
![image](https://github.com/user-attachments/assets/d1b4362b-316f-4ddf-b436-b9ca02c80520)

- Complete isolation from all dependencies
- Mock mode SCMI sysfs file/folder generator
- Mock mode data generator
- Gtest API bench testing suite
- Mock command line collector executable

# Installation and usage
TBD
