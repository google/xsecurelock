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

#ifndef WM_PROPERTIES_H
#define WM_PROPERTIES_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

void SetWMProperties(Display* dpy, Window w, const char* res_class,
                     const char* res_name, int argc, char* const* argv) {
  XClassHint* class_hint = XAllocClassHint();
  class_hint->res_name = (char*)res_name;
  class_hint->res_class = (char*)res_class;
  XTextProperty name_prop;
  XStringListToTextProperty((char**)&res_name, 1, &name_prop);
  XSetWMProperties(dpy, w, &name_prop, &name_prop, (char**)argv, argc, NULL,
                   NULL, class_hint);
  XFree(class_hint);
}

#endif
