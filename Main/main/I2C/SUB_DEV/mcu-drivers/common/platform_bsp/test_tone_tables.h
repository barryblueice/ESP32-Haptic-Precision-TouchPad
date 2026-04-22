/**
 * @file test_tone_tables.h
 *
 * @brief PCM sample tables for Render path Test Tone
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2021, 2023 All Rights Reserved, http://www.cirrus.com/
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

#ifndef TEST_TONE_TABLES_H
#define TEST_TONE_TABLES_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "stdint.h"

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/
/**
 * Build switch to include 100Hz sinewave tables
 *
 */
#define TEST_TONES_INCLUDE_100HZ

/**
 * Length of 1kHz sine period sampled at 48kHz in number of samples
 *
 * samples = (sample rate) / (fundamental frequency) = 48000 / 1000 = 48
 *
 */
#define PCM_1KHZ_SINGLE_PERIOD_LENGTH (48)

/**
 * Length of 1kHz sine period sampled at 48kHz, 32-bit samples in number of 2-byte words
 *
 * 2-byte words = (samples) * (2-byte words per 32-bit sample) = PCM_1KHZ_SINGLE_PERIOD_LENGTH * 2
 *
 */
#define PCM_1KHZ_SINGLE_PERIOD_LENGTH_2BYTES (PCM_1KHZ_SINGLE_PERIOD_LENGTH * 2)

/**
 * Length of 1kHz sine period sampled at 8kHz, 32-bit samples in number of 2-byte words
 *
 * 8 (1k tone period in samples at 8kHz) * 2
 *
 */
#define PCM_1KTONE_8kHz_SINGLE_PERIOD_LENGTH_2BYTES (8 * 2)

#ifdef TEST_TONES_INCLUDE_100HZ
/**
 * Length of 100Hz sine period sampled at 48kHz in number of samples
 *
 * #samples = sample rate / fundamental frequency = 48000 / 100 = 480
 */
#define PCM_100HZ_SINGLE_PERIOD_LENGTH (480)

/**
 * Length of 100Hz sine period sampled at 48kHz, 32-bit samples in number of 2-byte words
 *
 * 2-byte words = (samples) * (2-byte words per 32-bit sample) = PCM_100HZ_SINGLE_PERIOD_LENGTH * 2
 *
 */
#define PCM_100HZ_SINGLE_PERIOD_LENGTH_2BYTES (PCM_100HZ_SINGLE_PERIOD_LENGTH * 2)
#endif // TEST_TONES_INCLUDE_100HZ

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/
/**
 * Table of PCM samples for single period of stereo / 1kHz sine / -20dBFs / 32-bit
 *
 */
extern const uint16_t pcm_20dBFs_1kHz_32bit_stereo_single_period[PCM_1KHZ_SINGLE_PERIOD_LENGTH_2BYTES * 2];

#ifdef TEST_TONES_INCLUDE_100HZ
/**
 * Table of PCM samples for single period of stereo / 100Hz sine / -20dBFs / 32-bit
 *
 */
extern const uint16_t pcm_20dBFs_100Hz_32bit_stereo_single_period[PCM_100HZ_SINGLE_PERIOD_LENGTH_2BYTES * 2];
#endif // TEST_TONES_INCLUDE_100HZ

/**
 * Table of PCM samples for single period of stereo / silence / -20dBFs / 32-bit
 *
 */
extern const uint16_t pcm_silence_32bit_stereo_single_period[PCM_1KHZ_SINGLE_PERIOD_LENGTH_2BYTES * 2];

/**
 * Table of PCM samples for single period of stereo / 1kHz sine / -20dBFs / 16-bit
 *
 */
extern const uint16_t pcm_20dBFs_1kHz_16bit_stereo_single_period[PCM_1KHZ_SINGLE_PERIOD_LENGTH_2BYTES];

#ifdef TEST_TONES_INCLUDE_100HZ
/**
 * Table of PCM samples for single period of stereo / 100Hz sine / -20dBFs / 16-bit
 *
 */
extern const uint16_t pcm_20dBFs_100Hz_16bit_stereo_single_period[PCM_100HZ_SINGLE_PERIOD_LENGTH_2BYTES];
#endif // TEST_TONES_INCLUDE_100HZ

/**
 * Table of PCM samples for single period of stereo / silence / -20dBFs / 16-bit
 *
 */
extern const uint16_t pcm_silence_16bit_stereo_single_period[PCM_1KHZ_SINGLE_PERIOD_LENGTH_2BYTES];

/**
 * table of pcm samples for single period of stereo / 1khz sine / -20dbfs / 32-bit / 8khz sample rate
 *
 */
extern const uint16_t pcm_20dBFs_1kHz_32bit_8000_stereo_single_period[PCM_1KTONE_8kHz_SINGLE_PERIOD_LENGTH_2BYTES * 2];
/**
 * table of pcm samples for single period of stereo / 1khz sine / -20dbfs / 16-bit / 8khz sample rate
 *
 */
extern const uint16_t pcm_20dbfs_1kHz_16bit_8000_stereo_single_period[PCM_1KTONE_8kHz_SINGLE_PERIOD_LENGTH_2BYTES];
extern const uint16_t pcm_test[16 * 2];

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif /* TEST_TONE_TABLES_H */
