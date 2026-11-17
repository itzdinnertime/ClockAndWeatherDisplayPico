#include "services/credential_store.h"

#include <cstdio>

#include "hardware/flash.h"
#include "pico/flash.h"

namespace {
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
static_assert(tw::kCredentialRecordSize == FLASH_PAGE_SIZE, "record is exactly one flash page");

struct WriteJob {
    uint8_t page[tw::kCredentialRecordSize];
};

void writeSector(void* param) {
    const auto* job = static_cast<const WriteJob*>(param);
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset, job->page, FLASH_PAGE_SIZE);
}
}  // namespace

std::optional<tw::WifiCredentials> CredentialStore::load() const {
    const auto* stored = reinterpret_cast<const uint8_t*>(XIP_BASE + kFlashOffset);
    return tw::decodeCredentials(stored, tw::kCredentialRecordSize);
}

bool CredentialStore::save(const tw::WifiCredentials& creds) {
    static WriteJob job;  // must not live on a stack that could be in flux
    tw::encodeCredentials(creds, job.page);
    const int rc = flash_safe_execute(writeSector, &job, 1000);
    if (rc != PICO_OK) {
        std::printf("[setup] flash write failed: %d\n", rc);
        return false;
    }
    return load() == creds;  // read back through XIP to verify
}
