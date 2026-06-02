#pragma once

#include <QString>

namespace tpbr
{

/// Utility for registering the .tpbr file extension with Windows shell.
/// Uses per-user HKCU registry (no admin elevation required).
class FileAssociation
{
  public:
    /// Check if .tpbr is currently registered to this executable.
    static bool isRegistered();

    /// Register .tpbr file association for the current user.
    /// Returns true on success.
    static bool registerAssociation();

    /// Remove the .tpbr file association for the current user.
    /// Returns true on success.
    static bool unregisterAssociation();

  private:
    static constexpr const wchar_t* kExtension = L".tpbr";
    static constexpr const wchar_t* kProgId = L"TruePBR.Project";
    static constexpr const wchar_t* kFileTypeName = L"TruePBR Manager Project";
};

} // namespace tpbr
