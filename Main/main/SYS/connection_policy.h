#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Values match device_mode_t; -1 means startup has not selected a route yet. */
typedef struct {
    int target, boot;
    bool enabled, ble, manual, baseline, candidate, stable, valid, sampling;
    uint32_t candidate_at;
} connection_policy_t;
void connection_policy_init(connection_policy_t *p, int boot, bool enabled,
                            bool manual, bool baseline);
void connection_policy_enable(connection_policy_t *p, bool enabled);
void connection_policy_wake(connection_policy_t *p);
int connection_policy_sample(connection_policy_t *p, bool high, uint32_t now);
