# Manufacturer Source Integrity

[فارسی](README.fa.md) | **English**

This directory makes the provenance of the hardware references auditable.

- `source_catalog.csv` lists each reference, its authority, source type, scope, and why the project uses it.
- `official_files_sha256.txt` contains SHA-256 hashes for official manufacturer files stored locally in this repository.

URL shortcut files are intentionally not hashed because they point to live manufacturer pages that can be updated. Locally stored PDFs/images are hashed so an accidental replacement is detectable.
