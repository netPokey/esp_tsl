#include "analyzer/ws_protocol.h"
#include <cstring>

size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count)
{
    if (cap < 2)
        return 0;

    const size_t recSize = sizeof(WsFrameRecord);
    size_t maxByCap = (cap - 2) / recSize;
    if (maxByCap > count)
        maxByCap = count;

    buf[0] = WS_MSG_FRAME_DELTA;
    buf[1] = static_cast<uint8_t>(maxByCap);
    memcpy(buf + 2, recs, maxByCap * recSize);
    return 2 + maxByCap * recSize;
}

size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats)
{
    if (cap < 1 + sizeof(WsBusStats))
        return 0;

    buf[0] = WS_MSG_BUS_STATS;
    memcpy(buf + 1, &stats, sizeof(WsBusStats));
    return 1 + sizeof(WsBusStats);
}


namespace
{
template <typename T>
size_t wsBuildDiffRecords(uint8_t *buf, size_t cap, WsDiffSubtype subtype, const T *recs, uint8_t count)
{
    if (cap < 3)
        return 0;

    const size_t recSize = sizeof(T);
    size_t maxByCap = (cap - 3) / recSize;
    if (maxByCap > count)
        maxByCap = count;

    buf[0] = WS_MSG_DIFF;
    buf[1] = subtype;
    buf[2] = static_cast<uint8_t>(maxByCap);
    if (maxByCap > 0)
        memcpy(buf + 3, recs, maxByCap * recSize);
    return 3 + maxByCap * recSize;
}
}

size_t wsBuildSnapshotDiff(uint8_t *buf, size_t cap, const WsDiffRecord *recs, uint8_t count)
{
    return wsBuildDiffRecords(buf, cap, WS_DIFF_SNAPSHOT, recs, count);
}

size_t wsBuildPretrigger(uint8_t *buf, size_t cap, const WsPretriggerRecord *recs, uint8_t count)
{
    return wsBuildDiffRecords(buf, cap, WS_DIFF_PRETRIGGER, recs, count);
}

size_t wsBuildBaseline(uint8_t *buf, size_t cap, const WsBaselineRecord *recs, uint8_t count)
{
    return wsBuildDiffRecords(buf, cap, WS_DIFF_BASELINE, recs, count);
}
