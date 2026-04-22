/**
 * @file sdk_version.h
 *
 * @brief Alt-OS SDK version literals
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
 */

#ifndef SDK_VERSION_H
#define SDK_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

/**
 * @defgroup SDK_VERSION_
 * @brief Defines for the release version of the SDK
 *
 * @details
 * Versions for the SDK are defined as:
 * - Major - The interface of the firmware or module has changed in a way that breaks backwards compatibility. This
 * means that the module will not work as before if the old interface is used.
 * - Minor - The interface of the firmware or module has changed, but not in a way that breaks backwards compatibility.
 * This means that the module will work as before if the old interface is used.
 * - Update - The function has changed without changing the interface, for instance for a bug fix.
 *
 * @{
 */
#define SDK_VERSION_MAJOR       (4) ///< Release Major version
#define SDK_VERSION_MINOR       (29) ///< Release Minor version
#define SDK_VERSION_UPDATE      (4) ///< Release Update version
#define SDK_GIT_SHA             ("d93ccd246a11f6feeeb7af99ddc7a1ec1e769338") ///< Release Git SHA
/** @} */


/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // SDK_VERSION_H
