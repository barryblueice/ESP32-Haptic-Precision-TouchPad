/**
 * @file cs40l25_spec.h
 *
 * @brief Constants and Types from CS40L25 datasheet
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2019-2021 All Rights Reserved, http://www.cirrus.com/
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

#ifndef CS40L25_SPEC_H
#define CS40L25_SPEC_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include <stdint.h>

/***********************************************************************************************************************
 * GENERIC ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/
/**
 * Generic Value-to-Code Encoding Data Structure
 */
struct cs40l25_register_encoding
{
    uint32_t value; ///< Real-world value needing to be encoded
    uint8_t code;   ///< Code corresponding to value
};

/***********************************************************************************************************************
 * LITERALS, CONSTANTS, MACROS, ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/
/**
 * @defgroup CS40L25_DATASHEET
 * @brief All stuff from the datasheet
 *
 * @{
 */

/**
 * @defgroup SECTION_3_CHAR_AND_SPEC
 * @brief Characteristics and Specifications from datasheet
 *
 * @see Datasheet Section 3
 *
 * @{
 */
#define CS40L25_T_BST_PUP_MS                                                (1)             ///< @see Table 3-5
#define CS40L25_T_RLPW_MS                                                   (2)             ///< @see Table 3-14
#define CS40L25_T_IRS_MS                                                    (1)             ///< @see Table 3-14
/** @} */

/**
 * @defgroup SECTION_4_FUNCTIONAL_DESCRIPTION
 * @brief Functional Description from datasheet
 *
 * @see Datasheet Section 4
 *
 * @{
 */
/**
 * @defgroup SECTION_4_4_BHM
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 4.4.2
 *
 * @{
 */
#define DSP_BHM_HALO_HEARTBEAT_REG                                          (0x02800158)    ///< @see Section 4.4.2
#define DSP_BHM_STATEMACHINE_REG                                            (0X0280015C)    ///< @see Section 4.4.2
#define DSP_BHM_STATEMACHINE_SHUTDOWN                                       (0x4)           ///< @see Section 4.4.2
#define DSP_BHM_BUZZ_TRIGGER_REG                                            (0x02800188)    ///< @see Section 4.4.2
#define DSP_BHM_AMP_STATUS_REG                                              (0x0280018C)    ///< @see Section 4.4.2
#define DSP_BHM_AMP_STATUS_BOOT_DONE_BITMASK                                (0x1)           ///< @see Section 4.4.2
#define DSP_BHM_AMP_STATUS_OTP_ERROR_BITMASK                                (0x2)           ///< @see Section 4.4.2
#define DSP_BHM_AMP_STATUS_AMP_ERROR_BITMASK                                (0x4)           ///< @see Section 4.4.2
#define DSP_BHM_AMP_STATUS_TEMP_RISE_WARN_BITMASK                           (0x8)           ///< @see Section 4.4.2
#define DSP_BHM_AMP_STATUS_TEMP_ERROR_BITMASK                               (0x10)          ///< @see Section 4.4.2
#define DSP_BHM_AMP_SHUTDOWNREQUEST_REG                                     (0x02800190)    ///< @see Section 4.4.2
#define DSP_BHM_AMP_SHUTDOWNREQUEST_BITMASK                                 (0x1)           ///< @see Section 4.4.2
/** @} */

/**
 * Table for BST_K1 values based on L_BST and C_BST values
 *
 * Table is arranged as:
 * - index0 - L_BST value, in increasing order
 * - index1 - C_BST value, in increasing order
 *
 * @see BOOST_BST_LOOP_COEFF_REG
 * @see Section 4.6.3, Table 4-10
 *
 */
extern const uint8_t cs40l25_bst_k1_table[4][5];

/**
 * Table for BST_K2 values based on L_BST and C_BST values
 *
 * Table is arranged as:
 * - index0 - L_BST value, in increasing order
 * - index1 - C_BST value, in increasing order
 *
 * @see BOOST_BST_LOOP_COEFF_REG
 * @see Section 4.6.3, Table 4-10
 *
 */
extern const uint8_t cs40l25_bst_k2_table[4][5];

/**
 * Table for BST_SLOPE values based on L_BST
 *
 * Table is indexed by L_BST value, in increasing order
 *
 * @see BOOST_LBST_SLOPE_REG
 * @see Section 4.6.3, Table 4-10
 *
 */
extern const uint8_t cs40l25_bst_slope_table[4];
/** @} */


/**
 * @defgroup SECTION_4_13_AUDIO_SERIAL_PORT_DATA_INTERFACE
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 4.13
 *
 * @{
 */
/**
 * Beta value used to calculate value for CCM_FS_MON_0_REG
 *
 * @see Section 4.13.9
 *
 */
#define CS40L25_FS_MON0_BETA                                                (6000000)
/** @} */

/**
 * @defgroup SECTION_7_1_SW_RESET
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.1
 *
 * @{
 */
#define CS40L25_SW_RESET_DEVID_REG                                          (0x0)           ///< @see Section 7.1.1
#define CS40L25_SW_RESET_REVID_REG                                          (0x4)           ///< @see Section 7.1.2

#define CS40L25_DEVID                                                       (0x40a25a)      ///< @see Section 7.1.1
#define CS40L25B_DEVID                                                      (0x40a25b)      ///< @see Section 7.1.1
#define CS40L25_REVID_B1                                                    (0xB1)          ///< @see Section 7.1.2
/** @} */

/**
 * @defgroup SECTION_7_2_CTRL_KEYS
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.2
 *
 * @{
 */
#define CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG                                 (0x40)          ///< @see Section 7.2.1

#define CS40L25_TEST_KEY_CTRL_UNLOCK_1                                      (0x00000055)    ///< @see Section 7.2.1
#define CS40L25_TEST_KEY_CTRL_UNLOCK_2                                      (0x000000AA)    ///< @see Section 7.2.1
#define CS40L25_TEST_KEY_CTRL_LOCK_1                                        (0x000000CC)    ///< @see Section 7.2.1
#define CS40L25_TEST_KEY_CTRL_LOCK_2                                        (0x00000033)    ///< @see Section 7.2.1
/** @} */

/**
 * @defgroup SECTION_7_5_MSM
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.5
 *
 * @{
 */
#define MSM_GLOBAL_ENABLES_REG                                              (0x2014)        ///< @see Section 7.5.1
#define MSM_GLOBAL_ENABLES_GLOBAL_EN_BITMASK                                (0x1)           ///< @see Section 7.5.1

#define MSM_BLOCK_ENABLES_REG                                               (0x2018)        ///< @see Section 7.5.2
#define MSM_BLOCK_ENABLES_BST_EN_BITMASK                                    (0x30)          ///< @see Section 7.5.2
/**
 * Register definition for MSM_BLOCK_ENABLES_REG
 *
 * @see MSM_BLOCK_ENABLES_REG
 * @see Section 7.5.2
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t amp_en                     : 1;
        uint32_t reserved_0                 : 3;
        uint32_t bst_en                     : 2;
        uint32_t reserved_1                 : 2;
        uint32_t vpmon_en                   : 1;
        uint32_t vbstmon_en                 : 1;
        uint32_t tempmon_en                 : 1;
        uint32_t reserved_2                 : 1;
        uint32_t vmon_en                    : 1;
        uint32_t imon_en                    : 1;
        uint32_t reserved_3                 : 18;
    };
} cs40l25_msm_block_enables_t;

#define MSM_BLOCK_ENABLES2_REG                                              (0x201C)        ///< @see Section 7.5.3
/**
 * Register definition for MSM_BLOCK_ENABLES2_REG
 *
 * @see MSM_BLOCK_ENABLES2_REG
 * @see Section 7.5.3
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t reserved_0                 : 4;
        uint32_t classh_en                  : 1;
        uint32_t reserved_1                 : 3;
        uint32_t sync_en                    : 1;
        uint32_t reserved_2                 : 3;
        uint32_t vpbr_en                    : 1;
        uint32_t vbbr_en                    : 1;
        uint32_t reserved_3                 : 6;
        uint32_t amp_dre_en                 : 1;
        uint32_t reserved_4                 : 3;
        uint32_t wkfet_amp_en               : 1;
        uint32_t reserved_5                 : 7;
    };
} cs40l25_msm_block_enables2_t;

#define MSM_ERROR_RELEASE_REG                                               (0x2034)        ///< @see Section 7.5.6
/** @} */

/**
 * @defgroup SECTION_7_6_PAD_INTF
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.6
 *
 * @{
 */
#define CS40L25_GPIO_PAD_CONTROL_REG                                        (0x242c)        ///< @see Section 7.6.3

typedef union
{
    uint32_t word;

    struct
    {
        uint32_t reserved_0                 : 16;
        uint32_t gp1_ctrl                   : 3;
        uint32_t reserved_1                 : 5;
        uint32_t gp2_ctrl                   : 3;
        uint32_t reserved_2                 : 5;
    };
} cs40l25_gpio_pad_control_t;
/** @} */

/**
 * @defgroup SECTION_7_8_PWRMGT
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.8
 *
 * @{
 */
#define CS40L25_PWRMGT_CTL_REG                                              (0x2900)        ///< @see Section 7.8.1

#define CS40L25_PWRMGT_CTL_MEM_RDY_TRIG_HIBER                               (3)             ///< @see Section 7.8.1

#define CS40L25_WAKESRC_CTL_REG                                             (0x2904)        ///< @see Section 7.8.2

typedef union
{
    uint32_t word;

    struct
    {
        uint32_t wksrc_pol                  : 4;
        uint32_t wksrc_en                   : 4;
        uint32_t updt_wkctl                 : 1;
        uint32_t reserved                   : 23;
    };
} cs40l25_wakesrc_ctl_t;
/** @} */

/**
 * @defgroup SECTION_7_9_CCM
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.9
 *
 * @{
 */
#define CCM_REFCLK_INPUT_REG                                                (0x2C04)        ///< @see Section 7.9.1
#define CCM_REFCLK_INPUT_REG_DEFAULT                                        (0x00000010)    ///< @see Section 7.9.1

#define CS40L25_PLL_REFLCLK_SEL_BCLK                                        (0x0)           ///< @see Section 7.9.1
#define CS40L25_PLL_REFLCLK_SEL_FSYNC                                       (0x1)           ///< @see Section 7.9.1
#define CS40L25_PLL_REFLCLK_SEL_MCLK                                        (0x5)           ///< @see Section 7.9.1
/**
 * Register definition for CCM_REFCLK_INPUT_REG
 *
 * @see CCM_REFCLK_INPUT_REG
 * @see Section 7.9.1
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t pll_refclk_sel             : 3;
        uint32_t reserved_0                 : 1;
        uint32_t pll_refclk_en              : 1;
        uint32_t pll_refclk_freq            : 6;
        uint32_t pll_open_loop              : 1;
        uint32_t reserved_1                 : 4;
        uint32_t pll_force_en               : 1;
        uint32_t reserved_2                 : 15;
    };
} cs40l25_ccm_refclk_input_t;

/**
 * Encoding for CCM_REFCLK_INPUT_REG field PLL_REFCLK_FREQ
 *
 * Encodes from raw PLL input reference clock frequency in Hertz to bitfield code.
 *
 * @see CCM_REFCLK_INPUT_REG
 * @see Section 7.9.1
 *
 */
extern const struct cs40l25_register_encoding cs40l25_pll_sysclk[64];

#define CCM_FS_MON_0_REG                                                    (0x2D10)        ///< Reserved
/** @} */

/**
 * @defgroup SECTION_7_11_BOOST
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.11
 *
 * @{
 */
#define BOOST_VBST_CTL_1_REG                                                (0x3800)        ///< @see Section 7.11.1
/**
 * Register definition for BOOST_VBST_CTL_1_REG
 *
 * @see BOOST_VBST_CTL_1_REG
 * @see Section 7.11.1
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t bst_ctl                    : 8;
        uint32_t reserved                   : 24;
    };
} cs40l25_boost_vbst_ctl_1_t;

#define BOOST_VBST_CTL_2_REG                                                (0x3804)        ///< @see Section 7.11.2
/**
 * Register definition for BOOST_VBST_CTL_2_REG
 *
 * @see BOOST_VBST_CTL_2_REG
 * @see Section 7.11.2
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t bst_ctl_sel                : 2;
        uint32_t bst_ctl_lim_en             : 1;
        uint32_t reserved                   : 29;
    };
} cs40l25_boost_vbst_ctl_2_t;

#define BOOST_BST_IPK_CTL_REG                                               (0x3808)        ///< @see Section 7.11.3
/**
 * Register definition for BOOST_BST_IPK_CTL_REG
 *
 * @see BOOST_BST_IPK_CTL_REG
 * @see Section 7.11.3
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t bst_ipk                    : 7;
        uint32_t reserved                   : 25;
    };
} cs40l25_boost_bst_ipk_ctl_t;

#define BOOST_BST_LOOP_COEFF_REG                                            (0x3810)        ///< @see Section 7.11.5
/**
 * Register definition for BOOST_BST_LOOP_COEFF_REG
 *
 * @see BOOST_BST_LOOP_COEFF_REG
 * @see Section 7.11.5
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t bst_k1                     : 8;
        uint32_t bst_k2                     : 8;
        uint32_t reserved                   : 16;
    };
} cs40l25_boost_bst_loop_coeff_t;

#define BOOST_LBST_SLOPE_REG                                                (0x3814)        ///< @see Section 7.11.6
/**
 * Register definition for BOOST_LBST_SLOPE_REG
 *
 * @see BOOST_LBST_SLOPE_REG
 * @see Section 7.11.6
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t bst_lbst_val               : 2;
        uint32_t reserved_0                 : 6;
        uint32_t bst_slope                  : 8;
        uint32_t reserved_1                 : 16;
    };
} cs40l25_boost_lbst_slope_t;
/** @} */

/**
 * @defgroup SECTION_7_15_DATAIF
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.15
 *
 * @{
 */
#define DATAIF_ASP_ENABLES1_REG                                             (0x4800)        ///< @see Section 7.15.1
/**
 * Register definition for DATAIF_ASP_ENABLES1_REG
 *
 * @see DATAIF_ASP_ENABLES1_REG
 * @see Section 7.15.1
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t asp_tx1_en                 : 1;
        uint32_t asp_tx2_en                 : 1;
        uint32_t asp_tx3_en                 : 1;
        uint32_t asp_tx4_en                 : 1;
        uint32_t reserved_0                 : 12;
        uint32_t asp_rx1_en                 : 1;
        uint32_t asp_rx2_en                 : 1;
        uint32_t reserved_1                 : 14;
    };
} cs40l25_dataif_asp_enables1_t;

#define DATAIF_ASP_CONTROL1_REG                                             (0x4804)        ///< @see Section 7.15.2
/**
 * Register definition for DATAIF_ASP_CONTROL1_REG
 *
 * @see DATAIF_ASP_CONTROL1_REG
 * @see Section 7.15.2
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t asp_bclk_freq              : 6;
        uint32_t reserved                   : 16;
    };
} cs40l25_dataif_asp_control1_t;


/**
 * Encoding for DATAIF_ASP_CONTROL1_REG field ASP_BCLK_FREQ
 *
 * Encodes from raw ASP_BCLK frequency in Hertz to bitfield code.
 *
 * @see DATAIF_ASP_CONTROL1_REG
 * @see Section 7.15.2
 *
 */
extern const struct cs40l25_register_encoding cs40l25_sclk_encoding[48];

#define DATAIF_ASP_CONTROL2_REG                                             (0x4808)        ///< @see Section 7.15.3

#define CS40L25_ASP_CONTROL2_ASP_FMT_DSPA                                   (0x0)           ///< @see Section 7.15.3
#define CS40L25_ASP_CONTROL2_ASP_FMT_I2S                                    (0x2)           ///< @see Section 7.15.3
/**
 * Register definition for DATAIF_ASP_CONTROL2_REG
 *
 * @see DATAIF_ASP_CONTROL2_REG
 * @see Section 7.15.3
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t asp_fsync_mstr             : 1;
        uint32_t asp_fsync_frc              : 1;
        uint32_t asp_fsync_inv              : 1;
        uint32_t reserved_0                 : 1;
        uint32_t asp_bclk_mstr              : 1;
        uint32_t asp_bclk_frc               : 1;
        uint32_t asp_bclk_inv               : 1;
        uint32_t reserved_1                 : 1;
        uint32_t asp_fmt                    : 3;
        uint32_t reserved_2                 : 5;
        uint32_t asp_tx_width               : 8;
        uint32_t asp_rx_width               : 8;
    };
} cs40l25_dataif_asp_control2_t;

#define DATAIF_ASP_FRAME_CONTROL1_REG                                       (0x4810)        ///< Reserved
/**
 * Register definition for DATAIF_ASP_FRAME_CONTROL1_REG
 *
 * @see DATAIF_ASP_FRAME_CONTROL1_REG
 * @see Section 7.15.5
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t asp_tx1_slot               : 6;
        uint32_t reserved_0                 : 2;
        uint32_t asp_tx2_slot               : 6;
        uint32_t reserved_1                 : 2;
        uint32_t asp_tx3_slot               : 6;
        uint32_t reserved_2                 : 2;
        uint32_t asp_tx4_slot               : 6;
        uint32_t reserved_3                 : 2;
    };
} cs40l25_dataif_asp_frame_control1_t;

#define DATAIF_ASP_FRAME_CONTROL5_REG                                       (0x4820)        ///< @see Section 7.15.6
/**
 * Register definition for DATAIF_ASP_FRAME_CONTROL5_REG
 *
 * @see DATAIF_ASP_FRAME_CONTROL5_REG
 * @see Section 7.15.6
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t asp_rx1_slot               : 6;
        uint32_t reserved_0                 : 2;
        uint32_t asp_rx2_slot               : 6;
        uint32_t reserved_1                 : 18;
    };
} cs40l25_dataif_asp_frame_control5_t;

#define DATAIF_ASP_DATA_CONTROL1_REG                                        (0x4830)        ///< @see Section 7.15.7
/**
 * Register definition for DATAIF_ASP_DATA_CONTROL1_REG
 *
 * @see DATAIF_ASP_DATA_CONTROL1_REG
 * @see Section 7.15.7
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t asp_tx_wl                  : 6;
        uint32_t reserved                   : 26;
    };
} cs40l25_dataif_asp_data_control1_t;

#define DATAIF_ASP_DATA_CONTROL5_REG                                        (0x4840)        ///< @see Section 7.15.8
/**
 * Register definition for DATAIF_ASP_DATA_CONTROL5_REG
 *
 * @see DATAIF_ASP_DATA_CONTROL5_REG
 * @see Section 7.15.8
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t asp_rx_wl                  : 6;
        uint32_t reserved                   : 26;
    };
} cs40l25_dataif_asp_data_control5_t;
/** @} */

/**
 * @defgroup SECTION_7_16_MIXER
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.16
 *
 * @{
 */
#define CS40L25_MIXER_DACPCM1_INPUT_REG                                     (0x4C00)        ///< @see Section 7.16.1
#define CS40L25_MIXER_ASPTX1_INPUT_REG                                      (0x4C20)        ///< @see Section 7.16.2
#define CS40L25_MIXER_ASPTX2_INPUT_REG                                      (0x4C24)        ///< @see Section 7.16.3
#define CS40L25_MIXER_ASPTX3_INPUT_REG                                      (0x4C28)        ///< @see Section 7.16.4
#define CS40L25_MIXER_ASPTX4_INPUT_REG                                      (0x4C2C)        ///< @see Section 7.16.5
#define CS40L25_MIXER_DSP1RX1_INPUT_REG                                     (0x4C40)        ///< @see Section 7.16.6
#define CS40L25_MIXER_DSP1RX2_INPUT_REG                                     (0x4C44)        ///< @see Section 7.16.7
#define CS40L25_MIXER_DSP1RX3_INPUT_REG                                     (0x4C48)        ///< @see Section 7.16.7
#define CS40L25_MIXER_DSP1RX4_INPUT_REG                                     (0x4C4C)        ///< @see Section 7.16.7
#define CS40L25_MIXER_DSP1RX5_INPUT_REG                                     (0x4C50)        ///< @see Section 7.16.10
#define CS40L25_MIXER_DSP1RX6_INPUT_REG                                     (0x4C54)        ///< @see Section 7.16.11
#define CS40L25_MIXER_DSP1RX7_INPUT_REG                                     (0x4C58)        ///< @see Section 7.16.12
#define CS40L25_MIXER_DSP1RX8_INPUT_REG                                     (0x4C5C)        ///< @see Section 7.16.13

/**
 * @defgroup CS40L25_INPUT_SRC_
 * @brief Settings for MIXER Source Values
 *
 * @details See datasheet Section 7.16.1 - MIXER Source Values
 *
 * @{
 */
#define CS40L25_INPUT_SRC_ZERO_FILL                                         (0x00)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DIAG_GEN                                          (0x04)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_ASPRX1                                            (0x08)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_ASPRX2                                            (0x09)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_VMON                                              (0x18)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_IMON                                              (0x19)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_ERR_VOL                                           (0x20)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_CLASSH                                            (0x21)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_VPMON                                             (0x28)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_VBSTMON                                           (0x29)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DSP1TX1                                           (0x32)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DSP1TX2                                           (0x33)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DSP1TX3                                           (0x34)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DSP1TX4                                           (0x35)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DSP1TX5                                           (0x36)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DSP1TX6                                           (0x37)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DSP1TX7                                           (0x38)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_DSP1TX8                                           (0x39)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_TEMPMON                                           (0x3A)          ///< @see Section 7.16.1
#define CS40L25_INPUT_SRC_RSVD                                              (0x3B)          ///< @see Section 7.16.1
/** @} */

/**
 * Register definition for CS40L25_MIXER_DACPCM1_INPUT_REG to CS40L25_MIXER_DSP1RX8_INPUT_REG
 *
 * @see Sections 7.16.1 to 7.16.15
 * @see CS40L25_INPUT_SRC_
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t src                        : 7;
        uint32_t reserved                   : 25;
    };
} cs40l25_mixer_t;
/** @} */

/**
 * @defgroup SECTION_7_17_INTP
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.17
 *
 * @{
 */
#define CS40L25_INTP_AMP_CTRL_REG                                           (0x6000)        ///< @see Section 7.17.1
#define CS40L25_INTP_AMP_CTRL_AMP_VOL_PCM_BITOFFSET                         (0x3)           ///< @see Section 7.17.1
#define CS40L25_INTP_AMP_CTRL_AMP_VOL_PCM_BITWIDTH                          (0xb)           ///< @see Section 7.17.1

#define CS40L25_AMP_VOLUME_MUTE                                             (0x400)         ///< @see Section 7.17.1
#define CS40L25_AMP_VOLUME_0DB                                              (0)             ///< @see Section 7.17.1

/**
 * Register definition for CS40L25_INTP_AMP_CTRL_REG
 *
 * @see CS40L25_INTP_AMP_CTRL_REG
 * @see Sections 7.17.1
 *
 */
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t amp_ramp_pcm               : 3;
        uint32_t amp_vol_pcm                : 11;
        uint32_t amp_inv_pcm                : 1;
        uint32_t amp_hpf_pcm_en             : 1;
        uint32_t reserved                   : 16;
    };
} cs40l25_intp_amp_ctrl_t;
/** @} */

/**
 * @defgroup SECTION_7_23_IRQ1
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.23
 *
 * @{
 */
#define IRQ1_IRQ1_EINT_3_REG                                                (0x10018)       ///< @see Section 7.23.5
#define IRQ1_IRQ1_EINT_3_OTP_BOOT_ERR_BITMASK                               (0x80000000)    ///< @see Section 7.23.5
#define IRQ1_IRQ1_EINT_4_REG                                                (0x1001C)       ///< @see Section 7.23.6
#define IRQ1_IRQ1_EINT_4_BOOT_DONE_BITMASK                                  (0x2)           ///< @see Section 7.23.6
/** @} */

/**
 * @defgroup SECTION_7_24_IRQ2
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.24
 *
 * @{
 */
#define IRQ2_IRQ2_EINT_1_REG                                                (0x10810)       ///< @see Section 7.24.3
#define IRQ2_IRQ2_EINT_1_BST_OVP_ERR_EINT2_BITMASK                          (0x40)          ///< @see Section 7.24.3
#define IRQ2_IRQ2_EINT_1_BST_DCM_UVP_ERR_EINT2_BITMASK                      (0x80)          ///< @see Section 7.24.3
#define IRQ2_IRQ2_EINT_1_BST_SHORT_ERR_EINT2_BITMASK                        (0x100)         ///< @see Section 7.24.3
#define IRQ2_IRQ2_EINT_1_TEMP_WARN_RISE_EINT2_BITMASK                       (0x8000)        ///< @see Section 7.24.3
#define IRQ2_IRQ2_EINT_1_TEMP_ERR_EINT2_BITMASK                             (0x20000)       ///< @see Section 7.24.3
#define IRQ2_IRQ2_EINT_1_AMP_ERR_EINT2_BITMASK                              (0x80000000)    ///< @see Section 7.24.3
/** @} */

/**
 * @defgroup SECTION_7_28_DSP_VIRTUAL1_MBOX
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.28
 *
 * @{
 */
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_1_REG                           (0x13020)       ///< @see Section 7.28.1
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_2_REG                           (0x13024)       ///< @see Section 7.28.2
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG                           (0x1302C)       ///< @see Section 7.28.4
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_NONE                          (0x0)           ///< @see Section 7.28.4
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_WAKEUP                        (0x2)           ///< @see Section 7.28.4
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_FORCE_STANDBY                 (0x3)           ///< @see Section 7.28.4
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_REG                           (0x13030)       ///< @see Section 7.28.5
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_NONE                          (0x0)           ///< @see Section 7.28.5
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_START_I2S                     (0x2)           ///< @see Section 7.28.5
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_STOP_I2S                      (0x3)           ///< @see Section 7.28.5
#define DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_DISCHARGE_VAMP                (0x8)
/** @} */

/**
 * Definitions of dsp register types
 */
typedef union
{
    uint32_t word;

    struct {
        uint32_t halo_word    : 24;
        uint32_t reserved     : 8;
    };
} dsp_reg_t;

/**
 * @defgroup SECTION_7_36_XM_UNPACKED24
 * @brief Register descriptions from datasheet
 *
 * @see Datasheet Section 7.36
 *
 * @{
 */
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_RX1_REG                              (0x2B80080)     ///< @see Section 7.36.12
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_RX2_REG                              (0x2B80088)     ///< @see Section 7.36.13
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_RX3_REG                              (0x2B80090)     ///< @see Section 7.36.14
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_RX4_REG                              (0x2B80098)     ///< @see Section 7.36.15
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_RX5_REG                              (0x2B800A0)     ///< @see Section 7.36.16
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_RX6_REG                              (0x2B800A8)     ///< @see Section 7.36.17
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_RX7_REG                              (0x2B800B0)     ///< @see Section 7.36.18
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_RX8_REG                              (0x2B800B8)     ///< @see Section 7.36.19
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_TX1_REG                              (0x2B80280)     ///< @see Section 7.36.20
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_TX2_REG                              (0x2B80288)     ///< @see Section 7.36.21
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_TX3_REG                              (0x2B80290)     ///< @see Section 7.36.22
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_TX4_REG                              (0x2B80298)     ///< @see Section 7.36.23
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_TX5_REG                              (0x2B802A0)     ///< @see Section 7.36.24
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_TX6_REG                              (0x2B802A8)     ///< @see Section 7.36.25
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_TX7_REG                              (0x2B802B0)     ///< @see Section 7.36.26
#define XM_UNPACKED24_DSP1_SAMPLE_RATE_TX8_REG                              (0x2B802B8)     ///< @see Section 7.36.27
#define XM_UNPACKED24_DSP1_SCRATCH_REG                                      (0x2B805C0)     ///< @see Section 7.36.60
#define XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_REG                             (0x2BC1000)     ///< @see Section 7.36.64
#define XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_DSP1_CCM_CORE_EN_BITMASK        (0x1)           ///< @see Section 7.36.64
#define XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_DSP1_CCM_CORE_RESET_BITMASK     (0x200)         ///< @see Section 7.36.64

#define CS40L25_DSP1_SAMPLE_RATE_G1R2                                       (0x00000001)    ///< @see Section 7.36.12
/** @} */

/** @} */

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // CS40L25_SPEC_H
