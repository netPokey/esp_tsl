#include "analyzer/snapshot_store.h"
#include <cstring>

void SnapshotStore::init(SnapshotRecord *slotA, SnapshotRecord *slotB)
{
    a_ = slotA;
    b_ = slotB;

    if (a_)
        memset(a_, 0, sizeof(SnapshotRecord) * kChannelCount * kStdIdCount);
    if (b_)
        memset(b_, 0, sizeof(SnapshotRecord) * kChannelCount * kStdIdCount);
}

void SnapshotStore::capture(SnapshotSlot slot, const IdTable &table)
{
    SnapshotRecord *dst = slot == SnapshotSlot::A ? a_ : b_;
    if (!dst)
        return;

    for (uint8_t ch = 0; ch < kChannelCount; ++ch)
    {
        for (uint32_t id = 0; id < kStdIdCount; ++id)
        {
            const IdRecord &src = table.record(ch, id);
            SnapshotRecord &rec = dst[key(ch, id)];
            rec.present = src.present;
            rec.dlc = src.dlc;
            for (int i = 0; i < 8; ++i)
                rec.data[i] = src.data[i];
        }
    }
}

size_t SnapshotStore::diff(SnapshotDiffRecord *out, size_t cap) const
{
    return diff(out, cap, 0);
}

size_t SnapshotStore::diff(SnapshotDiffRecord *out, size_t cap, size_t skip) const
{
    if (!a_ || !b_ || !out || cap == 0)
        return 0;

    size_t n = 0;
    size_t skipped = 0;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch)
    {
        for (uint32_t id = 0; id < kStdIdCount; ++id)
        {
            const SnapshotRecord &ra = a_[key(ch, id)];
            const SnapshotRecord &rb = b_[key(ch, id)];
            uint8_t kind = 0;

            if (!ra.present && rb.present)
            {
                kind = SNAPSHOT_DIFF_ADDED;
            }
            else if (ra.present && !rb.present)
            {
                kind = SNAPSHOT_DIFF_REMOVED;
            }
            else if (ra.present && rb.present)
            {
                bool changed = ra.dlc != rb.dlc;
                for (int i = 0; i < 8 && !changed; ++i)
                    changed = ra.data[i] != rb.data[i];
                if (changed)
                    kind = SNAPSHOT_DIFF_CHANGED;
            }

            if (!kind)
                continue;
            if (skipped < skip)
            {
                ++skipped;
                continue;
            }
            if (n >= cap)
                return n;

            SnapshotDiffRecord &dst = out[n++];
            dst.channel = ch;
            dst.id = static_cast<uint16_t>(id);
            dst.kind = kind;
            dst.dlc_a = ra.dlc;
            dst.dlc_b = rb.dlc;
            for (int i = 0; i < 8; ++i)
            {
                dst.data_a[i] = ra.data[i];
                dst.data_b[i] = rb.data[i];
            }
        }
    }

    return n;
}
