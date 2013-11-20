// Copyright (c) 2013 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef XWALK_RUNTIME_BROWSER_XWALK_APP_EXTENSION_BRIDGE_H_
#define XWALK_RUNTIME_BROWSER_XWALK_APP_EXTENSION_BRIDGE_H_

#include "xwalk/application/browser/application_system.h"
#include "xwalk/extensions/browser/xwalk_extension_service.h"
#include "xwalk/extensions/common/xwalk_extension_permission_types.h"

namespace xwalk {

class XWalkAppExtensionBridge
    : public extensions::XWalkExtensionService::Delegate {
 public:
  XWalkAppExtensionBridge(
      application::ApplicationSystem* app_system);
  virtual ~XWalkAppExtensionBridge();

  // XWalkExtensionService::Delegate implementation
  virtual void CheckAPIAccessControl(std::string extension_name,
      std::string api_name,
      const extensions::PermissionCallback& callback) OVERRIDE;

 private:
  application::ApplicationSystem* app_system_;

  DISALLOW_COPY_AND_ASSIGN(XWalkAppExtensionBridge);
};

}  // namespace xwalk

#endif  // XWALK_RUNTIME_BROWSER_XWALK_APP_EXTENSION_BRIDGE_H_
