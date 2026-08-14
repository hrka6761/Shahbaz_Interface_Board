# Project Hardware Configuration Records

[فارسی](README.fa.md) | **English**

These files are machine-readable engineering records used by project validation tools. They describe **what Shahbaz is configured to use**, **what is manufacturer-backed**, **what still requires physical verification**, and the product-role contract: `shahbaz_interface_board` is the USB Device, Android + Shahbaz is the operational host/client, and Windows is development/HIL only.

- `project_hardware_profile.yaml` — target board/module, I2C, USB, memory, power, sensor-module identity status, and safety defaults.
- `project_wiring.csv` — connection-by-connection wiring contract.
- `gpio_usage_rules.csv` — GPIO assignment/reservation/restriction policy.
- `hardware_photos.yaml` — expected evidence photos, hashes, and verification status.
- `hardware_measurements.yaml` — measurements that should be recorded from the assembled hardware.

Do not manually change a value just to make a validator pass. A status such as `UNVERIFIED` is intentional when the exact physical evidence is not yet available.

## Firmware evidence authorization

`tools/validate_firmware_contract.py` cross-checks firmware defaults/protocol versions against these records. During the ESP-IDF build it also evaluates enabled Kconfig verification claims against the actual evidence manifests. A nonempty evidence ID alone is not authorization: the referenced record must be present and have an eligible physical/measurement status and purpose.
