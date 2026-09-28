#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Values match device_mode_t; negative values never authorize input. */
enum { CONNECTION_WAIT = -1, CONNECTION_PROBE = -2 };
typedef struct {
    int target, boot;
    bool enabled, ble, manual, baseline, candidate, stable, valid, sampling, choose;
    uint32_t candidate_at;
} connection_policy_t;
void connection_policy_init(connection_policy_t *p, int boot, bool enabled,
                            bool manual, bool baseline);
void connection_policy_enable(connection_policy_t *p, bool enabled);
void connection_policy_wake(connection_policy_t *p);
int connection_policy_sample(connection_policy_t *p, bool high, uint32_t now);
