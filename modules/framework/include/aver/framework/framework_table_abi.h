#ifndef AVER_FRAMEWORK_TABLE_ABI_H
#define AVER_FRAMEWORK_TABLE_ABI_H

/* Data table C ABI (Aver.Framework): .csv / table-shaped .json files as named rows of named columns.
 * Cells stay text; the C# DataTable binder converts. Separate header with its own version so
 * framework_abi.h is untouched. Conventions as framework_timers_abi.h: int32_t/const char* only, UTF-8,
 * single game thread, handle 0 == invalid. Semantics: docs/DATA_TABLES.md. */

#include "aver/framework/framework_abi.h"   /* AVER_FW_ABI */

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_FW_TABLE_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_table_abi_version(void);

#define AVER_FW_TABLE_COL_STRING  0
#define AVER_FW_TABLE_COL_INT     1
#define AVER_FW_TABLE_COL_FLOAT   2
#define AVER_FW_TABLE_COL_BOOL    3
#define AVER_FW_TABLE_COL_VEC3    4
#define AVER_FW_TABLE_COL_TEXT    5
#define AVER_FW_TABLE_COL_STRINGS 6

/* The project's content root. Relative table paths resolve against it; hosts set it when a project opens.
 * Returns 1, 0 for NULL. */
AVER_FW_ABI int32_t aver_fw_content_set_root(const char* utf8Dir);
AVER_FW_ABI const char* aver_fw_content_root(void);

/* Loads (or returns the existing handle for) a .csv/.json table. Path is absolute or content-relative; the
 * key is the normalised absolute path. Returns a handle > 0, or 0 with aver_fw_table_error() set. */
AVER_FW_ABI int32_t aver_fw_table_load(const char* utf8Path);
AVER_FW_ABI const char* aver_fw_table_error(void);
AVER_FW_ABI const char* aver_fw_table_path(int32_t h);                /* absolute; "" invalid */
AVER_FW_ABI int32_t aver_fw_table_unload(int32_t h);                  /* 1 if it was loaded */
/* Re-reads the file. A parse failure (or a missing file) KEEPS the old data (a half-saved file must not wipe
 * a live table), sets the error, returns 0. Success swaps the data, bumps the table's version and the global
 * epoch, returns 1. */
AVER_FW_ABI int32_t aver_fw_table_reload(int32_t h);
/* Count of loaded tables reloaded successfully; "" or NULL = reload every loaded table (watcher overflow). */
AVER_FW_ABI int32_t aver_fw_table_reload_path(const char* utf8AbsPath);
AVER_FW_ABI int32_t aver_fw_table_version(int32_t h);                 /* -1 invalid; starts at 1 */
AVER_FW_ABI int32_t aver_fw_tables_epoch(void);                       /* bumps on any successful (re)load */
AVER_FW_ABI int32_t aver_fw_table_column_count(int32_t h);            /* excludes the row-name column; -1 invalid */
AVER_FW_ABI const char* aver_fw_table_column_name(int32_t h, int32_t c);
AVER_FW_ABI int32_t aver_fw_table_column_type(int32_t h, int32_t c);  /* AVER_FW_TABLE_COL_* (advisory); -1 out of range */
AVER_FW_ABI int32_t aver_fw_table_column_find(int32_t h, const char* name);   /* -1 */
AVER_FW_ABI int32_t aver_fw_table_row_count(int32_t h);               /* -1 invalid */
AVER_FW_ABI const char* aver_fw_table_row_name(int32_t h, int32_t r);
AVER_FW_ABI int32_t aver_fw_table_row_find(int32_t h, const char* name);      /* -1 */
AVER_FW_ABI const char* aver_fw_table_cell(int32_t h, int32_t r, int32_t c);  /* "" out of range */
/* The const char* from row_name/cell/column_name point into table storage: valid until that table is
 * reloaded or unloaded. error/path/content_root: until the next call that returns a string. */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AVER_FRAMEWORK_TABLE_ABI_H */
