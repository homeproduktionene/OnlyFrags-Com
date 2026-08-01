// ─────────────────────────────────────────────────────────────────────────────
// OnlyFrags.rc  –  OnlyFrags Com
// Win32 resource script: manifest embed + version information.
// ─────────────────────────────────────────────────────────────────────────────

#include <windows.h>

// ── Embed the application manifest (visual styles + DPI awareness) ────────────
1 RT_MANIFEST "..\\manifest\\app.manifest"

// ── Version information ───────────────────────────────────────────────────────
VS_VERSION_INFO VERSIONINFO
FILEVERSION    1, 0, 0, 0
PRODUCTVERSION 1, 0, 0, 0
FILEFLAGSMASK  0x3fL
FILEFLAGS      0x0L
FILEOS         VOS__WINDOWS32
FILETYPE       VFT_APP
FILESUBTYPE    VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040704B0"
        BEGIN
            VALUE "CompanyName",      "OnlyFrags"
            VALUE "FileDescription",  "OnlyFrags Com – Voice Chat Client"
            VALUE "FileVersion",      "1.0.0.0"
            VALUE "InternalName",     "OnlyFrags"
            VALUE "LegalCopyright",   "Copyright (C) 2025 OnlyFrags"
            VALUE "OriginalFilename", "OnlyFrags.exe"
            VALUE "ProductName",      "OnlyFrags Com"
            VALUE "ProductVersion",   "1.0.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0407, 1200
    END
END
