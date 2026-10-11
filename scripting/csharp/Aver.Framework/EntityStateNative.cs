// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for framework_state_abi.h (managed entity state that rides in a save).

using System.Runtime.InteropServices;

namespace Aver.Framework;

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate int StateCountFn(int entity, IntPtr user);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate int StateAtFn(int entity, int index, IntPtr nameBuf, int nameCap, IntPtr dataBuf, int dataCap, IntPtr user);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate int StateSetFn(int entity, IntPtr name, IntPtr data, int dataLen, IntPtr user);

/// <summary>The managed entity state C ABI (framework_state_abi.h). Declared here for the same reason as
/// <see cref="FwTE"/>: only this assembly has the DllImport resolver.</summary>
internal static class FwState
{
    private const string Lib = "Aver.Framework";

    [DllImport(Lib)] internal static extern int aver_fw_state_abi_version();
    [DllImport(Lib)] internal static extern int aver_fw_set_state_provider(StateCountFn? count, StateAtFn? at, StateSetFn? setState, IntPtr user);
    [DllImport(Lib)] internal static extern int aver_fw_state_count(int entity);
    [DllImport(Lib)] internal static extern int aver_fw_state_at(int entity, int index, byte[] nameBuf, int nameCap, byte[] dataBuf, int dataCap);
    [DllImport(Lib)] internal static extern int aver_fw_state_set(int entity, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, byte[] data, int dataLen);
}
