// Copyright (c) 2013 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xwalk/application/common/permission_types.h"

namespace xwalk {
namespace application {

const std::string StoredPermissionStr[] = {
    "ALLOW",
    "DENY",
    "PROMPT",
};

std::string StoredPermToString(const StoredPermission permission) {
  if (permission == INVALID_STORED_PERM)
    return std::string("");
  return StoredPermissionStr[permission];
}

StoredPermission StringToStoredPerm(const std::string& str) {
  int i;
  for (i = 0; i < INVALID_STORED_PERM; ++i) {
    if (str == StoredPermissionStr[i])
      break;
  }
  return static_cast<StoredPermission>(i);
}

}  // namespace application
}  // namespace xwalk
