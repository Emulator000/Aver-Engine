// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for framework_table_abi.h.

using System.Runtime.InteropServices;

namespace Aver.Framework;

/// <summary>The data-table C ABI (framework_table_abi.h).</summary>
internal static class FwTable
{
    private const string Lib = "Aver.Framework";

    [DllImport(Lib)] internal static extern int aver_fw_table_abi_version();
    [DllImport(Lib)] internal static extern IntPtr aver_fw_content_root();
    [DllImport(Lib)] internal static extern int aver_fw_table_load([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Lib)] internal static extern IntPtr aver_fw_table_error();
    [DllImport(Lib)] internal static extern IntPtr aver_fw_table_path(int handle);
    [DllImport(Lib)] internal static extern int aver_fw_table_reload(int handle);
    [DllImport(Lib)] internal static extern int aver_fw_table_version(int handle);
    [DllImport(Lib)] internal static extern int aver_fw_tables_epoch();
    [DllImport(Lib)] internal static extern int aver_fw_table_column_count(int handle);
    [DllImport(Lib)] internal static extern IntPtr aver_fw_table_column_name(int handle, int column);
    [DllImport(Lib)] internal static extern int aver_fw_table_column_type(int handle, int column);
    [DllImport(Lib)] internal static extern int aver_fw_table_column_find(int handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Lib)] internal static extern int aver_fw_table_row_count(int handle);
    [DllImport(Lib)] internal static extern IntPtr aver_fw_table_row_name(int handle, int row);
    [DllImport(Lib)] internal static extern int aver_fw_table_row_find(int handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Lib)] internal static extern IntPtr aver_fw_table_cell(int handle, int row, int column);

    /// <summary>Decodes a native UTF-8 pointer; empty when null.</summary>
    internal static string Str(IntPtr p) => Marshal.PtrToStringUTF8(p) ?? string.Empty;
}
