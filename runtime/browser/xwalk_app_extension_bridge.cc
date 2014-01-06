// Copyright (c) 2013 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xwalk/application/browser/application.h"
#include "xwalk/application/browser/application_system.h"
#include "xwalk/application/browser/application_service.h"
#include "xwalk/runtime/browser/xwalk_app_extension_bridge.h"

namespace xwalk {

XWalkAppExtensionBridge::XWalkAppExtensionBridge(
      application::ApplicationSystem* app_system)
  : app_system_(app_system) {
}

XWalkAppExtensionBridge::~XWalkAppExtensionBridge() {}

void XWalkAppExtensionBridge::CheckAPIAccessControl(std::string extension_name,
    std::string api_name, const extensions::PermissionCallback& callback) {
  xwalk::application::ApplicationService* service =
      app_system_->application_service();
  // TODO(Bai): The extension system should be aware where the request is
  // comming from, i.e. the request origin application ID. So, apart from
  // the rp-ep mapping, we need an additional mapping for AppID-rp.
  xwalk::application::Application* running_app =
      service->GetActiveApplication();
  if (running_app) {
    std::string app_id = running_app->id();
    service->CheckAPIAccessControl(app_id, extension_name, api_name,
       *(reinterpret_cast<const xwalk::application::PermissionCallback*>
        (&callback)));
  } else {
    // We don't support permission check for simple browser mode.
    callback.Run(extensions::INVALID_RUNTIME_PERM);
  }
}

bool XWalkAppExtensionBridge::RegisterPermissions(std::string extension_name,
    std::string perm_table) {
  xwalk::application::ApplicationService* service =
        app_system_->application_service();
  xwalk::application::Application* running_app =
        service->GetActiveApplication();
  if (running_app) {
    std::string app_id = running_app->id();
    return service->RegisterPermissions(app_id, extension_name, perm_table);
  } else {
    return false;
  }
}

}  // namespace xwalk

