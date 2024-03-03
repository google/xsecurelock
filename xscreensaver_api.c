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

#include "xscreensaver_api.h"

#include <X11/X.h>   // for Window
#include <stdio.h>   // for fprintf, stderr
#include <stdlib.h>  // for setenv

#include "env_settings.h"  // for GetUnsignedLongLongSetting
#include "logging.h"

void ExportWindowID(Window w) {
  char window_id_str[32];
  int window_id_len = snprintf(window_id_str, sizeof(window_id_str), "%llu",
                               (unsigned long long)w);
  if (window_id_len <= 0 || (size_t)window_id_len >= sizeof(window_id_str)) {
    Log("Window ID doesn't fit into buffer");
    return;
  }
  setenv("XSCREENSAVER_WINDOW", window_id_str, 1);
}

void ExportSaverIndex(int index) {
  char saver_index_str[32];
  int saver_index_len = snprintf(saver_index_str, sizeof(saver_index_str), "%llu",
                                 (unsigned long long)index);
  if (saver_index_len <= 0 || (size_t)saver_index_len >= sizeof(saver_index_str)) {
    Log("Saver index doesn't fit into buffer");
    return;
  }
  setenv("XSCREENSAVER_SAVER_INDEX", saver_index_str, 1);
}

Window ReadWindowID(void) {
  return GetUnsignedLongLongSetting("XSCREENSAVER_WINDOW", None);
}
