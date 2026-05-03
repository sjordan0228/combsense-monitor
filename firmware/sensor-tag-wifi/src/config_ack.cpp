#include "config_ack.h"

#include <cstring>

using ConfigParser::FeatFlag;

namespace {

/// Resolve the post-apply state of a feat flag given the parsed value and the
/// current NVS state.  "Absent" in parsed → keep current NVS value.
static bool resolveFlag(FeatFlag parsed, bool currentNvs) {
    if (parsed == FeatFlag::Absent) return currentNvs;
    return parsed == FeatFlag::On;
}

static void appendEntry(AckEntry* entries, size_t* count,
                        const char* key, const char* result) {
    if (count == nullptr) return;
    AckEntry& e = entries[*count];
    strncpy(e.key,    key,    sizeof(e.key)    - 1); e.key[sizeof(e.key) - 1] = '\0';
    strncpy(e.result, result, sizeof(e.result) - 1); e.result[sizeof(e.result) - 1] = '\0';
    (*count)++;
}

}  // namespace

bool preValidate(const ConfigParser::ConfigUpdate& parsed,
                 const TemperatureNvsState& nvsState,
                 AckEntry* outEntries, size_t* outCount) {
    if (outCount) *outCount = 0;

    // Compute post-apply state for the two mutually-exclusive temp sensors.
    bool ds18b20_after = resolveFlag(parsed.feat_ds18b20, nvsState.ds18b20_enabled);
    bool sht31_after   = resolveFlag(parsed.feat_sht31,   nvsState.sht31_enabled);

    if (ds18b20_after && sht31_after) {
        // Both would be enabled — report conflict on whichever key(s) the
        // payload touched.  If neither key was in the payload this branch is
        // unreachable (both would mirror the NVS state which should never be
        // simultaneously enabled), but guard defensively.
        if (outEntries && outCount) {
            // Report on feat_sht31: conflicts with feat_ds18b20
            appendEntry(outEntries, outCount, "feat_sht31",   "conflict:feat_ds18b20");
            // Report on feat_ds18b20: conflicts with feat_sht31
            appendEntry(outEntries, outCount, "feat_ds18b20", "conflict:feat_sht31");
        }
        return false;
    }

    return true;
}
