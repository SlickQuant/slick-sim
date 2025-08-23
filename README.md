# ExchangeSimulator

A cross-platform exchange matching engine simulator written in modern C++23.

## Building

```bash
cmake -S . -B build
cmake --build build
```

Pass optional flags to enable developer tooling:

```bash
cmake -S . -B build -DENABLE_ASAN=ON          # AddressSanitizer
cmake -S . -B build -DENABLE_CLANG_TIDY=ON    # clang-tidy analysis
```

Requires the [spdlog](https://github.com/gabime/spdlog) library to be available to the build system.
