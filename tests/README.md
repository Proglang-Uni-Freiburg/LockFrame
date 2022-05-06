# Tests

Tests use `Google Test` and `CMake`.

To run tests, execute `cmake --build build && ctest --test-dir build`.

After a change of the CMakeLists.txt file, you have to run `cmake -S . -B build`.

More information: https://google.github.io/googletest/quickstart-cmake.html