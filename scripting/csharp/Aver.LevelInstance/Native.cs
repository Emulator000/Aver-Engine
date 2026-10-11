// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for the Aver.LevelInstance.Abi C ABI
// (modules/levelinstance.abi/include/aver/levelinstance/level_instance_abi.h).
using System.Runtime.InteropServices;

namespace Aver.LevelInstance;

/// <summary>One extern per aver_levelinst_* export, spelled exactly as the header spells it.</summary>
internal static class Native
{
    private const string Lib = "Aver.LevelInstance.Abi";

    [DllImport(Lib)] internal static extern int aver_levelinst_abi_version();

    [DllImport(Lib)] internal static extern int aver_levelinst_spawn(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path, int parent,
        float x, float y, float z, float yaw, float pitch, float roll, float scale, int packed);
    [DllImport(Lib)] internal static extern int aver_levelinst_destroy(int root);
    [DllImport(Lib)] internal static extern int aver_levelinst_root_of(int entity);
    [DllImport(Lib)] internal static extern int aver_levelinst_is_member(int entity);
    [DllImport(Lib)] internal static extern int aver_levelinst_find(int root,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idPath);
    [DllImport(Lib)] internal static extern int aver_levelinst_set_loaded(int root, int loaded);
    [DllImport(Lib)] internal static extern int aver_levelinst_is_loaded(int root);
    [DllImport(Lib)] internal static extern int aver_levelinst_reload(int root);
    [DllImport(Lib)] internal static extern int aver_levelinst_set_packed(int root, int packed);
    [DllImport(Lib)] internal static extern int aver_levelinst_bounds(int root, float[] minXyz, float[] maxXyz);
    [DllImport(Lib)] internal static extern int aver_levelinst_count();
    [DllImport(Lib)] internal static extern int aver_levelinst_at(int index);
    [DllImport(Lib)] internal static extern int aver_levelinst_asset_id(int root, out long outId);

    /// <summary>Why the last level-instance call on THIS thread failed. See <see cref="LevelInstanceError"/>.</summary>
    [DllImport(Lib)] internal static extern int aver_levelinst_last_error();
}

/// <summary>Why a level-instance call failed, mirroring aver::AbiError in
/// modules/core/include/aver/core/ErrorCodes.hpp. Every value except <see cref="Ok"/> is negative,
/// so <c>(int)code &lt; 0</c> means "failed" even for a code added after this assembly was built.</summary>
public enum LevelInstanceError
{
    /// <summary>No error was recorded by the last call that records one.</summary>
    Ok = 0,
    /// <summary>The root is not a live instance (the host refused it).</summary>
    BadHandle = -1,
    /// <summary>A required out-parameter was null.</summary>
    NullPointer = -2,
    /// <summary>No host is installed: the runtime never installed a LevelInstances.</summary>
    NotInitialised = -3,
    /// <summary>An index past the end of what exists.</summary>
    OutOfRange = -4,
    /// <summary>The installed host does not provide this function.</summary>
    Unsupported = -5,
    /// <summary>A null or empty level path, or a spawn the host refused.</summary>
    InvalidArgument = -6,
    /// <summary>The request was legal and the memory was not there.</summary>
    AllocationFailed = -7,
}
