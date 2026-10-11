# Aver.LevelInstance.Abi  (`modules/levelinstance.abi`)

- **Language:** C++ (a plain-C surface)
- **Depends on:** Core (derived from Scene: it is built exactly when the scene module is)
- **Built:** SHARED, stateless -- the .NET layer (`Aver.LevelInstance`) P/Invokes it

The plain-C seam a script places, streams and queries **level instances** through: spawn a runtime-only instance of
a `.ocworld`, destroy it, ask which instance an entity belongs to, find a child placement by its id path, load and
unload it by hand, reload it, switch it between packed and unpacked, ask its bounds, enumerate instances.

The DLL holds no state. The host (the editor, the runtime) owns `game::LevelInstances` and installs function
pointers with `aver_levelinst_set_host`, built by `game::makeLevelInstanceAbiHost` (`Runtime/include/aver/game/
LevelInstancesAbiHost.hpp`). Until a host is installed every call returns 0 and `aver_levelinst_last_error` reads
`NotInitialised`.

| Header | What it owns |
|---|---|
| `level_instance_abi.h` | The plain-C functions and the `AverLevelInstanceHost` table |

The design, the `LEVELINST` record, the editor workflow, packed mode and its limits are in
[docs/LEVEL_INSTANCES.md](../../docs/LEVEL_INSTANCES.md). Tests: `tests/levelinstance` (`LevelInstanceAbiTest`).
