# UTF-8 Decoder (technically transcoder to UTF-32)

This is my personal implementation of a UTF-8 decoder based on the DFAs built by Björn Höhrmann and Bob Seagall, released into the public domain.
It is currently WIP, as it is missing correct noexcept propagation. Beyond that it *should* work on MSVC/Clang/GCC.
More documentation and examples are also pending.

It performs no allocations of its own, and will not throw exceptions. It also does not rely on the STL beyond some simple type definitions.

Note: It is exclusively little-endian, but is otherwise platform independent.

## Example

More examples will be coming, for now here's a trivial example.
```cpp

std::vector<char> input{ 0xE2, 0x88, 0x85 };
std::vector<char32_t> output{};

auto _ = bkh::encoding::utf8::decode(std::begin(input), std::end(input), std::back_inserter(output));

//'output' should now contain the single character U+2205 (EMPTY SET)
```
