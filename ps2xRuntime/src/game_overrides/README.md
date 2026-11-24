# Game overrides

Put game override `.cpp` files here. Every `*.cpp` in this folder is compiled into `ps2EntryRunner`.

Overrides register through a static initializer (`PS2_REGISTER_GAME_OVERRIDE`, see
`ps2xRuntime/include/game_overrides.h`). A file compiled into the `ps2_runtime` static library is
never referenced, so the linker drops it and the override silently never runs. Keep them here.
