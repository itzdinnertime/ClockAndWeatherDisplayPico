#pragma once
// Persists Wi-Fi credentials in the last 4 KB sector of flash.

#include <optional>

#include "tw/provisioning.h"

class CredentialStore {
public:
    std::optional<tw::WifiCredentials> load() const;
    // Erase + program via flash_safe_execute(), which parks core 1 (it runs
    // from flash too) for the ~50 ms the sector is unavailable.
    bool save(const tw::WifiCredentials& creds);
};
