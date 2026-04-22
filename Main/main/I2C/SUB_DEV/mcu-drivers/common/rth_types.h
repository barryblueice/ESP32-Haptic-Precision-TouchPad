/**
 * @file rth_types.h
 *
 * @brief Contains the typedefs related to the RTH APIs
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2022, 2026 All Rights Reserved, http://www.cirrus.com/
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifndef RTHTYPES_H
#define RTHTYPES_H

#ifdef __cplusplus
extern "C" {
#endif
/***********************************************************************************************************************
   * INCLUDES
   **********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/***********************************************************************************************************************
   * LITERALS & CONSTANTS
   **********************************************************************************************************************/
typedef struct
{
    uint16_t duration;
    uint16_t level;
    uint16_t freq;
    bool chirp;
    bool half_cycles;
} rth_pwle_section_t;

typedef struct
{
   const rth_pwle_section_t** sections;
   uint16_t num_sections;
   uint32_t length_us;
   const char* name;
   uint32_t msft_id;
} rth_pwle_t;

#ifdef __cplusplus
}
#endif

#endif // RTHTYPES_H
