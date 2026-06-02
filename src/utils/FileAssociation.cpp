#include "FileAssociation.h"

#include "Log.h"

#include <QCoreApplication>
#include <QDir>

#include <Windows.h>
#include <ShlObj.h>

namespace tpbr
{

/// Helper: open or create a registry key under HKCU.
static HKEY openOrCreateKey(const std::wstring& subKey)
{
    HKEY hKey = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                  nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("Failed to create registry key: {} (error {})", QString::fromStdWString(subKey).toStdString(),
                     result);
        return nullptr;
    }
    return hKey;
}

/// Helper: set a string value on a registry key.
static bool setStringValue(HKEY hKey, const wchar_t* valueName, const std::wstring& data)
{
    LONG result = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
                                 static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    return result == ERROR_SUCCESS;
}

/// Helper: read a string value from a registry key.
static std::wstring getStringValue(HKEY hParent, const std::wstring& subKey, const wchar_t* valueName)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hParent, subKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return {};

    wchar_t buffer[512] = {};
    DWORD bufSize = sizeof(buffer);
    DWORD type = 0;
    LONG result = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bufSize);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || type != REG_SZ)
        return {};

    return std::wstring(buffer);
}

/// Helper: recursively delete a registry key tree.
static bool deleteKeyTree(HKEY hParent, const wchar_t* subKey)
{
    LONG result = RegDeleteTreeW(hParent, subKey);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool FileAssociation::isRegistered()
{
    // Check if .tpbr points to our ProgId
    std::wstring progId = getStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\.tpbr", nullptr);
    if (progId != kProgId)
        return false;

    // Check if the ProgId's open command points to our executable
    std::wstring commandKey = std::wstring(L"Software\\Classes\\") + kProgId + L"\\shell\\open\\command";
    std::wstring command = getStringValue(HKEY_CURRENT_USER, commandKey, nullptr);

    if (command.empty())
        return false;

    // Verify the command contains our executable path
    QString exePath = QCoreApplication::applicationFilePath();
    QString nativePath = QDir::toNativeSeparators(exePath);
    return command.find(nativePath.toStdWString()) != std::wstring::npos;
}

bool FileAssociation::registerAssociation()
{
    const QString exePath = QCoreApplication::applicationFilePath();
    const std::wstring nativeExePath = QDir::toNativeSeparators(exePath).toStdWString();
    const std::wstring openCommand = L"\"" + nativeExePath + L"\" \"%1\"";
    const std::wstring iconValue = L"\"" + nativeExePath + L"\",0";

    bool success = true;

    // 1. Register the extension: HKCU\Software\Classes\.tpbr -> TruePBR.Project
    {
        HKEY hKey = openOrCreateKey(L"Software\\Classes\\.tpbr");
        if (!hKey)
            return false;
        success &= setStringValue(hKey, nullptr, kProgId);
        RegCloseKey(hKey);
    }

    // 2. Register the ProgId: HKCU\Software\Classes\TruePBR.Project
    {
        std::wstring progIdKey = std::wstring(L"Software\\Classes\\") + kProgId;
        HKEY hKey = openOrCreateKey(progIdKey);
        if (!hKey)
            return false;
        success &= setStringValue(hKey, nullptr, kFileTypeName);
        RegCloseKey(hKey);
    }

    // 3. DefaultIcon
    {
        std::wstring iconKey = std::wstring(L"Software\\Classes\\") + kProgId + L"\\DefaultIcon";
        HKEY hKey = openOrCreateKey(iconKey);
        if (!hKey)
            return false;
        success &= setStringValue(hKey, nullptr, iconValue);
        RegCloseKey(hKey);
    }

    // 4. shell\open\command
    {
        std::wstring commandKey = std::wstring(L"Software\\Classes\\") + kProgId + L"\\shell\\open\\command";
        HKEY hKey = openOrCreateKey(commandKey);
        if (!hKey)
            return false;
        success &= setStringValue(hKey, nullptr, openCommand);
        RegCloseKey(hKey);
    }

    // 5. Notify the shell that file associations have changed
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    if (success)
        SPDLOG_INFO("Registered .tpbr file association for: {}", exePath.toStdString());
    else
        SPDLOG_ERROR("Failed to register .tpbr file association");

    return success;
}

bool FileAssociation::unregisterAssociation()
{
    bool success = true;

    // Delete the ProgId tree
    success &= deleteKeyTree(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + kProgId).c_str());

    // Delete the extension key
    success &= deleteKeyTree(HKEY_CURRENT_USER, L"Software\\Classes\\.tpbr");

    // Notify the shell
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    if (success)
        SPDLOG_INFO("Unregistered .tpbr file association");

    return success;
}

} // namespace tpbr
