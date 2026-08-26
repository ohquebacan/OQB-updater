// Comandos de ns:am que libnx no expone y que hacen falta para registrar
// el forwarder en el menú HOME. Portado de sphaira (GPLv3).
#pragma once

#include <switch.h>

#include "forwarder/nx_types.hpp"

namespace fwd::ns_ex {

enum ApplicationRecordType
{
    ApplicationRecordType_Running = 0x0,
    ApplicationRecordType_Installed = 0x3,
    ApplicationRecordType_Downloading = 0x4,
    ApplicationRecordType_GamecardMissing = 0x5,
    ApplicationRecordType_Downloaded = 0x6,
    ApplicationRecordType_Updated = 0xA,
    ApplicationRecordType_Archived = 0xB,
};

Result Initialize();
void Exit();

Result PushApplicationRecord(u64 tid, const ContentStorageRecord* records, u32 count);
Result InvalidateApplicationControlCache(u64 tid);

}  // namespace fwd::ns_ex
