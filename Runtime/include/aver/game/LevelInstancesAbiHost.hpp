#pragma once
// The host half of level_instance_abi.h: an AverLevelInstanceHost whose function pointers drive a LevelInstances.
// The host installs it once, after creating its game level:
//     static AverLevelInstanceHost h = aver::game::makeAbiHost(level.instances());
//     aver_levelinst_set_host(&h);                                   // from Aver.LevelInstance.Abi
#include "aver/game/LevelInstances.hpp"

#if AVER_MODULE_SCENE && defined(AVER_WITH_LEVELINST_ABI)
#  include "aver/levelinstance/level_instance_abi.h"

namespace aver::game {

// `li` must outlive the host table; the pointers capture its address.
AverLevelInstanceHost makeAbiHost(LevelInstances& li);

} // namespace aver::game

#endif
