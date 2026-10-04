# ESP-Brookesia Display Service

* [中文版本](./README_CN.md)

## Overview

`brookesia_service_display` is the display arbitration service for LCD outputs, touch input, and presentation sources.

For more information, see the [ESP-Brookesia Programming Guide](https://docs.espressif.com/projects/esp-brookesia/en/latest/service/display.html).

## DataFlow Integration

The service registers a visual DataFlow provider for output and source discovery, active-source routing, and frame presentation. Video consumers can use the provider-neutral visual operation without depending on this component's implementation details.

## How to Use

### Environment Requirements

Please refer to the following documentation:

- [ESP-Brookesia Programming Guide - Versioning](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-versioning)
- [ESP-Brookesia Programming Guide - Development Environment Setup](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-dev-environment)

### Add to Your Project

Please refer to [ESP-Brookesia Programming Guide - How to Obtain and Use Components](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-component-usage).
