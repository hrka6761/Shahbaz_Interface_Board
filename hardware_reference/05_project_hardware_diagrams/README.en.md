# Project Hardware Diagrams

[فارسی](README.fa.md) | **English**

These Mermaid files are editable diagrams generated from the same hardware assumptions recorded in `../04_project_hardware_configuration/`.

Read them in order:

1. `01_system_wiring.mmd` — operational Android + Shahbaz path, plus the separate Windows HIL diagnostic path.
2. `02_i2c_bus.mmd` — shared I2C bus and module-specific address/strap configuration.
3. `03_usb_ports.mmd` — native ESP32-S3 USB versus the separate CH343P USB-to-UART connector.
4. `04_power_and_usb_vbus.mmd` — power/USB-host safety boundary.
5. `05_gpio_usage.mmd` — assigned, reserved, unavailable, and review-required GPIO groups.

The diagrams are documentation sources, not physical proof. `UNVERIFIED` labels must remain where a claim still depends on matching or measuring the exact hardware.
