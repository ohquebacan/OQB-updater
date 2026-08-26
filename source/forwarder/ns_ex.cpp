#include "forwarder/ns_ex.hpp"

#include "service_guard.h"

namespace fwd::ns_ex {
namespace {

    Service g_nsAppSrv;

    NX_GENERATE_SERVICE_GUARD(nsEx);

    Result _nsExInitialize()
    {
        Result rc = nsInitialize();
        if (R_FAILED(rc)) return rc;

        if (hosversionAtLeast(3, 0, 0)) {
            rc = nsGetApplicationManagerInterface(&g_nsAppSrv);
            if (R_FAILED(rc)) {
                nsExit();
                return rc;
            }
        }
        else {
            g_nsAppSrv = *nsGetServiceSession_ApplicationManagerInterface();
        }

        return 0;
    }

    void _nsExCleanup()
    {
        serviceClose(&g_nsAppSrv);
        nsExit();
    }

}  // namespace

Result Initialize()
{
    return nsExInitialize();
}

void Exit()
{
    nsExExit();
}

Result PushApplicationRecord(u64 tid, const ContentStorageRecord* records, u32 count)
{
    const struct
    {
        u8 last_modified_event;
        u8 padding[0x7];
        u64 tid;
    } in = {ApplicationRecordType_Installed, {0}, tid};

    return serviceDispatchIn(&g_nsAppSrv, 16, in,
                             .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                             .buffers = {{records, sizeof(*records) * count}});
}

Result InvalidateApplicationControlCache(u64 tid)
{
    return serviceDispatchIn(&g_nsAppSrv, 404, tid);
}

}  // namespace fwd::ns_ex
