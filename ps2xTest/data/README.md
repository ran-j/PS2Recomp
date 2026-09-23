# VU test fixtures

The `vu_*.vublocks` files contain authored instruction sequences for static VU
compilation tests. They are synthetic programs, not captured game microcode.
They follow this repository's [license](../../LICENSE).

The corresponding [runtime tests](../src/ps2_vu1_tests.cpp) construct the same
programs with instruction-encoding helpers and check registers, pipeline timing,
branches, memory writes, and execution-budget boundaries. Named builders include
`writeCountedLoopCode`, `writeEarlyCounterLoopCode`, `writeDivLoopCode`,
`writeDivClipLoopCode`, `writeLongLoopCode`, and `writeTerminalBranchFixture`;
the other cases construct their sequences within the tests.

[`compile_vu_blocks.py`](../../tools/compile_vu_blocks.py) consumes these profiles
to generate native instantiations and a dispatch registry. Its
[tests](../../tools/tests/test_compile_vu_blocks.py) also create synthetic input
profiles and code images. The profile format contains instruction records only,
so provenance is documented here rather than inside the fixture files.

Runtime captures, game code images, snapshots, and native sources generated from
game inputs remain local build artifacts. Do not add them to this directory.
