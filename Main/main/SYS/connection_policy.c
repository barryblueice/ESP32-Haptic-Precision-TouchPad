#include "connection_policy.h"

void connection_policy_init(connection_policy_t *p, int boot, bool enabled,
                            bool manual, bool baseline)
{
    *p = (connection_policy_t){.target = boot, .boot = boot, .enabled = enabled,
        .ble = manual && boot == 2, .manual = manual, .baseline = baseline,
        .stable = baseline, .choose = enabled && !manual};
    if (p->choose) p->target = CONNECTION_WAIT;
}
void connection_policy_enable(connection_policy_t *p, bool enabled)
{
    if (!enabled && p->target < 0) p->target = p->boot;
    if (enabled && !p->enabled) {
        p->manual = false;
        p->choose = true;
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
        bool changed = high != p->stable;
        p->stable = high; p->valid = true;
        if (p->manual && high != p->baseline) p->manual = false;
        if (p->enabled && !p->ble && !p->manual && (p->choose || changed)) {
            p->target = high ? 0 : CONNECTION_PROBE;
            p->choose = false;
        }
    }
    return p->target;
}
