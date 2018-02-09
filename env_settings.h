/*
Copyright 2018 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef ENV_SETTINGS_H
#define ENV_SETTINGS_H

/*! \brief Loads an integer setting from the environment.
 *
 * \param name The setting to read (with XSECURELOCK_ variable name prefix).
 * \param def The default value.
 * \return The value of the setting, or def if unset or not a number.
 */
unsigned long long GetUnsignedLongLongSetting(const char* name,
                                              unsigned long long def);

/*! \brief Loads an integer setting from the environment.
 *
 * \param name The setting to read (with XSECURELOCK_ variable name prefix).
 * \param def The default value.
 * \return The value of the setting, or def if unset or not a number.
 */
long GetLongSetting(const char* name, long def);

/*! \brief Loads an integer setting from the environment.
 *
 * \param name The setting to read (with XSECURELOCK_ variable name prefix).
 * \param def The default value.
 * \return The value of the setting, or def if unset or not a number.
 */
int GetIntSetting(const char* name, int def);

/*! \brief Loads a setting from the environment.
 *
 * \param name The setting to read (with XSECURELOCK_ variable name prefix).
 * \param def The default value.
 * \return The value of the setting, or def if unset or empty.
 */
const char* GetStringSetting(const char* name, const char* def);

#endif
