
/**
 * @file cs40l25_sym.h
 *
 * @brief Master table of known firmware symbols for the CS40L25 Driver module
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2026 All Rights Reserved, http://www.cirrus.com/
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
 * firmware_converter.py SDK version: 4.29.5 - f8e3db79b513ea4cf1fd88d04c0ebd10f4f0e311
 * Command:  tools\firmware_converter\firmware_converter.py fw_img_v2 cs40l25 analysis\surface_capsule\pseudo_wmfw\SurfaceTouchpadHaptic_2.9.139_payload_0_body_after_header\SurfaceTouchpadHaptic_2.9.139_payload_0_body_after_header_pseudo.wmfw --output-directory analysis\surface_capsule\pseudo_wmfw\fw_img_out
 *
 *
 */

#ifndef CS40L25_SYM_H
#define CS40L25_SYM_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

/**
 * @defgroup CS40L25_ALGORITHMS
 * @brief Defines indicating presence of HALO Core Algorithms
 *
 * @{
 */
#define CS40L25_ALGORITHM_SURFACE_SAML_HAPTIC
/** @} */

/**
 * @defgroup CS40L25_SYM_
 * @brief Single source of truth for firmware symbols known to the driver.
 *
 * @{
 */
// SURFACE_SAML_HAPTIC
#define CS40L25_SYM_SURFACE_SAML_HAPTIC_SURFACE_DIR_XM_STRUCT_T (0x1)
#define CS40L25_SYM_SURFACE_SAML_DIRECTORY_BLOB                 (0x2)
#define CS40L25_SYM_SURFACE_SAML_TAIL_AI_BLOB                   (0x3)
#define CS40L25_SYM_SURFACE_SAML_BRIDGE_SAC_1330                (0x4)
#define CS40L25_SYM_SURFACE_SAML_BRIDGE_AC_1340                 (0x5)
#define CS40L25_SYM_SURFACE_SAML_REGION_B_C000_WINDOW           (0x6)
#define CS40L25_SYM_SURFACE_SAML_REGION_B_8000_HEAD             (0x7)
#define CS40L25_SYM_SURFACE_SAML_REGION_B_F002_WINDOW           (0x8)
#define CS40L25_SYM_SURFACE_SAML_REGION_B_FE30_WINDOW           (0x9)
#define CS40L25_SYM_SURFACE_SAML_REGION_B_FEB8_WINDOW           (0xa)
#define CS40L25_SYM_SURFACE_SAML_REGION_B_10000_WINDOW          (0xb)
#define CS40L25_SYM_SURFACE_SAML_REGION_B_10001_WINDOW          (0xc)
#define CS40L25_SYM_SURFACE_SAML_REGION_A_4159_WINDOW           (0xd)
#define CS40L25_SYM_SURFACE_SAML_REGION_A_4500_WINDOW           (0xe)
#define CS40L25_SYM_SURFACE_SAML_REGION_A_55ED_WINDOW           (0xf)
#define CS40L25_SYM_SURFACE_SAML_REGION_A_6009_WINDOW           (0x10)
#define CS40L25_SYM_SURFACE_SAML_REGION_A_2000_WINDOW           (0x11)
#define CS40L25_SYM_SURFACE_SAML_REGION_A_2020_WINDOW           (0x12)

/** @} */

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // CS40L25_SYM_H

