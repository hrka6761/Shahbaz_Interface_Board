# Application Utilities

[فارسی](README.fa.md) | **English**

Small deterministic utilities used by the application layer.

## Main files

- `include/shahbaz/application/fixed_priority_queue.hpp`: bounded priority queue that orders work without heap allocation.
- `test/fixed_priority_queue_test.cpp`: verifies ordering, capacity, full/empty behavior, and boundaries.
- `CMakeLists.txt`: registers the header-only C++17 component.

This component has no ESP-IDF runtime or hardware dependency.
