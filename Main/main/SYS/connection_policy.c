#include "connection_policy.h"

void connection_policy_init(connection_policy_t *p, int boot, bool enabled,
                            bool manual, bool baseline)
{
    *p = (connection_policy_t){.target = boot, .boot = boot, .enabled = enabled,
        .ble = boot == 2, .manual = manual && enabled, .baseline = baseline};
    if (enabled && !manual && !p->ble) p->target = -1;
}
void connection_policy_enable(connection_policy_t *p, bool enabled)
{
    if (!enabled && p->target < 0) p->target = p->boot;
    if (enabled && !p->enabled) {
        p->manual = false;
        /* A confirmed level can be used immediately; a wake still needs debounce. */
        if (!p->ble && p->valid) p->target = p->stable ? 0 : 1;
    }
    p->enabled = enabled;
}
void connection_policy_wake(connection_policy_t *p)
{
    p->sampling = p->valid = false;
}
int connection_policy_sample(connection_policy_t *p, bool high, uint32_t now)
{
    if (!p->sampling || high != p->candidate) {
        p->candidate = high; p->candidate_at = now; p->sampling = true;
    }
    if ((uint32_t)(now - p->candidate_at) >= 300U) {
        p->stable = high; p->valid = true;
        if (p->manual && high != p->baseline) p->manual = false;
        if (p->enabled && !p->ble && !p->manual) p->target = high ? 0 : 1;
    }
    return p->target;
}
