# libcrunch

A Zstandard decoder written from scratch in C++17 against RFC 8878, with no
dependencies and the spec as the only reference. Validated against the worked
examples in the RFC.

## Building

```
cmake -B build
cmake --build build
ctest --test-dir build
```
