// Copyright (c) 2013 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xwalk/application/browser/application_service.h"

#include <set>
#include <string>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "xwalk/application/browser/application.h"
#include "xwalk/application/browser/application_event_manager.h"
#include "xwalk/application/browser/application_storage.h"
#include "xwalk/application/browser/application_system.h"
#include "xwalk/application/browser/installer/package.h"
#include "xwalk/application/common/application_file_util.h"
#include "xwalk/application/common/application_manifest_constants.h"
#include "xwalk/application/common/event_names.h"
#include "xwalk/application/common/id_util.h"
#include "xwalk/application/common/permission_policy_manager.h"
#include "xwalk/runtime/browser/runtime.h"
#include "xwalk/runtime/browser/runtime_context.h"
#include "xwalk/runtime/browser/xwalk_runner.h"

#if defined(OS_TIZEN_MOBILE)
#include "xwalk/application/browser/installer/tizen/package_installer.h"
#endif

using xwalk::RuntimeContext;

namespace {

void CloseMessageLoop() {
  // FIXME: Quit message loop here at present. This should go away once
  // we have Application in place.
  base::MessageLoop::current()->QuitWhenIdle();
}

void WaitForEventAndClose(
    const std::string& app_id, const std::string& event_name,
    xwalk::application::ApplicationEventManager* event_manager) {
  class CloseOnEventArrived : public xwalk::application::EventObserver {
   public:
    static CloseOnEventArrived* Create(const std::string& event_name,
        xwalk::application::ApplicationEventManager* event_manager) {
      return new CloseOnEventArrived(event_name, event_manager);
    }

    virtual void Observe(
        const std::string& app_id,
        scoped_refptr<xwalk::application::Event> event) OVERRIDE {
      DCHECK(xwalk::application::kOnJavaScriptEventAck == event->name());
      std::string ack_event_name;
      event->args()->GetString(0, &ack_event_name);
      if (ack_event_name != event_name_)
        return;
      CloseMessageLoop();
      delete this;
    }

   private:
    CloseOnEventArrived(
        const std::string& event_name,
        xwalk::application::ApplicationEventManager* event_manager)
        : xwalk::application::EventObserver(event_manager),
          event_name_(event_name) {}

    std::string event_name_;
  };

  DCHECK(event_manager);
  CloseOnEventArrived* observer =
      CloseOnEventArrived::Create(event_name, event_manager);
  event_manager->AttachObserver(app_id,
      xwalk::application::kOnJavaScriptEventAck, observer);
}

void WaitForFinishLoad(
    scoped_refptr<xwalk::application::ApplicationData> application,
    xwalk::application::ApplicationEventManager* event_manager,
    content::WebContents* contents) {
  class CloseAfterLoadObserver : public content::WebContentsObserver {
   public:
    CloseAfterLoadObserver(
        scoped_refptr<xwalk::application::ApplicationData> application,
        xwalk::application::ApplicationEventManager* event_manager,
        content::WebContents* contents)
        : content::WebContentsObserver(contents),
          application_(application),
          event_manager_(event_manager) {
      DCHECK(application_);
      DCHECK(event_manager_);
    }

    virtual void DidFinishLoad(
        int64 frame_id,
        const GURL& validate_url,
        bool is_main_frame,
        content::RenderViewHost* render_view_host) OVERRIDE {
      if (!IsEventHandlerRegistered(xwalk::application::kOnInstalled)) {
        CloseMessageLoop();
      } else {
        scoped_ptr<base::ListValue> event_args(new base::ListValue);
        scoped_refptr<xwalk::application::Event> event =
            xwalk::application::Event::CreateEvent(
                xwalk::application::kOnInstalled, event_args.Pass());
        event_manager_->SendEvent(application_->ID(), event);

        WaitForEventAndClose(
            application_->ID(), event->name(), event_manager_);
      }
      delete this;
    }

   private:
    bool IsEventHandlerRegistered(const std::string& event_name) const {
      const std::set<std::string>& events = application_->GetEvents();
      return events.find(event_name) != events.end();
    }

    scoped_refptr<xwalk::application::ApplicationData> application_;
    xwalk::application::ApplicationEventManager* event_manager_;
  };

  // This object is self-destroyed when an event occurs.
  new CloseAfterLoadObserver(application, event_manager, contents);
}

#if defined(OS_TIZEN_MOBILE)
bool InstallPackageOnTizen(xwalk::application::ApplicationService* service,
                           xwalk::application::ApplicationStorage* storage,
                           const std::string& app_id,
                           const base::FilePath& data_dir) {
  // FIXME(cmarcelo): The Tizen-specific steps of installation in
  // service mode are not supported yet. Remove when this is fixed.
  if (xwalk::XWalkRunner::GetInstance()->is_running_as_service())
    return true;

  scoped_ptr<xwalk::application::PackageInstaller> installer =
      xwalk::application::PackageInstaller::Create(service, storage,
                                                   app_id, data_dir);
  if (!installer || !installer->Install()) {
    LOG(ERROR) << "An error occurred during installation on Tizen.";
    return false;
  }
  return true;
}

bool UninstallPackageOnTizen(xwalk::application::ApplicationService* service,
                             xwalk::application::ApplicationStorage* storage,
                             const std::string& app_id,
                             const base::FilePath& data_dir) {
  // FIXME(cmarcelo): The Tizen-specific steps of installation in
  // service mode are not supported yet. Remove when this is fixed.
  if (xwalk::XWalkRunner::GetInstance()->is_running_as_service())
    return true;

  scoped_ptr<xwalk::application::PackageInstaller> installer =
      xwalk::application::PackageInstaller::Create(service, storage,
                                                   app_id, data_dir);
  if (!installer || !installer->Uninstall()) {
    LOG(ERROR) << "An error occurred during uninstallation on Tizen.";
    return false;
  }
  return true;
}
#endif  // OS_TIZEN_MOBILE

}  // namespace

namespace xwalk {
namespace application {

const base::FilePath::CharType kApplicationsDir[] =
    FILE_PATH_LITERAL("applications");

ApplicationService::ApplicationService(RuntimeContext* runtime_context,
                                       ApplicationStorage* app_storage,
                                       ApplicationEventManager* event_manager)
    : runtime_context_(runtime_context),
      application_storage_(app_storage),
      event_manager_(event_manager),
      permission_policy_manager_(new PermissionPolicyManager()) {
}

ApplicationService::~ApplicationService() {
}

bool ApplicationService::Install(const base::FilePath& path, std::string* id) {
  if (!base::PathExists(path))
    return false;

  const base::FilePath data_dir =
      runtime_context_->GetPath().Append(kApplicationsDir);

  // Make sure the kApplicationsDir exists under data_path, otherwise,
  // the installation will always fail because of moving application
  // resources into an invalid directory.
  if (!base::DirectoryExists(data_dir) &&
      !file_util::CreateDirectory(data_dir))
    return false;

  base::FilePath unpacked_dir;
  std::string app_id;
  if (!base::DirectoryExists(path)) {
    scoped_ptr<Package> package = Package::Create(path);
    if (package)
      app_id = package->Id();

    if (app_id.empty()) {
      LOG(ERROR) << "XPK/WGT file is invalid.";
      return false;
    }

    if (application_storage_->Contains(app_id)) {
      *id = app_id;
      LOG(INFO) << "Already installed: " << app_id;
      return false;
    }

    base::FilePath temp_dir;
    package->Extract(&temp_dir);
    unpacked_dir = data_dir.AppendASCII(app_id);
    if (base::DirectoryExists(unpacked_dir) &&
        !base::DeleteFile(unpacked_dir, true))
      return false;
    if (!base::Move(temp_dir, unpacked_dir))
      return false;
  } else {
    unpacked_dir = path;
  }

  std::string error;
  scoped_refptr<ApplicationData> application_data =
      LoadApplication(unpacked_dir,
                      app_id,
                      Manifest::COMMAND_LINE,
                      &error);
  if (!application_data) {
    LOG(ERROR) << "Error during application installation: " << error;
    return false;
  }

  if (!permission_policy_manager_->InitApplicationPermission(application_data.get())) {
    LOG(ERROR) << "Application permission data is invalid";
    return false;
  }

  if (!application_storage_->AddApplication(application_data)) {
    LOG(ERROR) << "Application with id " << application_data->ID()
               << " couldn't be installed.";
    return false;
  }

#if defined(OS_TIZEN_MOBILE)
  if (!InstallPackageOnTizen(this, application_storage_,
                             application_data->ID(),
                             runtime_context_->GetPath()))
    return false;
#endif

  LOG(INFO) << "Installed application with id: " << application_data->ID()
            << " successfully.";
  *id = application_data->ID();

  FOR_EACH_OBSERVER(Observer, observers_,
                    OnApplicationInstalled(application_data->ID()));

  // We need to run main document after installation in order to
  // register system events.
  if (application_data->HasMainDocument()) {
    if (Application* application = Launch(application_data->ID())) {
      WaitForFinishLoad(application->data(), event_manager_,
          application->GetMainDocumentRuntime()->web_contents());
    }
  }

  return true;
}

bool ApplicationService::Uninstall(const std::string& id) {
#if defined(OS_TIZEN_MOBILE)
  if (!UninstallPackageOnTizen(this, application_storage_, id,
                               runtime_context_->GetPath()))
    return false;
#endif

  if (!application_storage_->RemoveApplication(id)) {
    LOG(ERROR) << "Cannot uninstall application with id " << id
               << "; application is not installed.";
    return false;
  }

  const base::FilePath resources =
      runtime_context_->GetPath().Append(kApplicationsDir).AppendASCII(id);
  if (base::DirectoryExists(resources) &&
      !base::DeleteFile(resources, true)) {
    LOG(ERROR) << "Error occurred while trying to remove application with id "
               << id << "; Cannot remove all resources.";
    return false;
  }

  FOR_EACH_OBSERVER(Observer, observers_, OnApplicationUninstalled(id));

  return true;
}

Application* ApplicationService::Launch(const std::string& id) {
  scoped_refptr<ApplicationData> application_data =
    application_storage_->GetApplicationData(id);
  if (!application_data) {
    LOG(ERROR) << "Application with id " << id << " is not installed.";
    return NULL;
  }

  return Launch(application_data);
}

Application* ApplicationService::Launch(const base::FilePath& path) {
  if (!base::DirectoryExists(path))
    return NULL;

  std::string error;
  scoped_refptr<ApplicationData> application_data =
      LoadApplication(path, Manifest::COMMAND_LINE, &error);

  if (!application_data) {
    LOG(ERROR) << "Error occurred while trying to launch application: "
               << error;
    return NULL;
  }

  return Launch(application_data);
}

Application* ApplicationService::Launch(const GURL& url) {
  namespace keys = xwalk::application_manifest_keys;

  const std::string& url_spec = url.spec();
  DCHECK(!url_spec.empty());
  const std::string& app_id = GenerateId(url_spec);
  // FIXME: we need to handle hash collisions.
  DCHECK(!application_storage_->GetApplicationData(app_id));

  base::DictionaryValue manifest;
  // FIXME: define permissions!
  manifest.SetString(keys::kLaunchWebURLKey, url_spec);
  manifest.SetString(keys::kNameKey, "XWalk Browser");
  manifest.SetString(keys::kVersionKey, "0");
  manifest.SetInteger(keys::kManifestVersionKey, 1);
  std::string error;
  scoped_refptr<ApplicationData> application_data = ApplicationData::Create(
            base::FilePath(), Manifest::COMMAND_LINE, manifest, app_id, &error);
  if (!application_data) {
    LOG(ERROR) << "Error occurred while trying to launch application: "
               << error;
    return NULL;
  }

  return Launch(application_data, Application::LaunchWebURLKey);
}

Application* ApplicationService::GetActiveApplication() const {
  if (applications_.empty())
    return NULL;
  return applications_[0];  // Return the first item in the list.
}

namespace {

struct ApplicationRenderHostIDComparator {
    explicit ApplicationRenderHostIDComparator(int id) : id(id) { }
    bool operator() (Application* application) {
      return id == application->GetRenderProcessHostID();
    }
    int id;
};

struct ApplicationIDComparator {
    explicit ApplicationIDComparator(const std::string& app_id)
      : app_id(app_id) { }
    bool operator() (Application* application) {
      return app_id == application->id();
    }
    std::string app_id;
};

}  // namespace

Application* ApplicationService::GetApplicationByRenderHostID(int id) const {
  ApplicationRenderHostIDComparator comparator(id);
  ScopedVector<Application>::const_iterator found = std::find_if(
      applications_.begin(), applications_.end(), comparator);
  if (found != applications_.end())
    return *found;
  return NULL;
}

Application* ApplicationService::GetApplicationByID(
    const std::string& app_id) const {
  ApplicationIDComparator comparator(app_id);
  ScopedVector<Application>::const_iterator found = std::find_if(
      applications_.begin(), applications_.end(), comparator);
  if (found != applications_.end())
    return *found;
  return NULL;
}

void ApplicationService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ApplicationService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ApplicationService::OnApplicationTerminated(
                                      Application* application) {
  ScopedVector<Application>::iterator found = std::find(
      applications_.begin(), applications_.end(), application);
  CHECK(found != applications_.end());
  FOR_EACH_OBSERVER(Observer, observers_,
                    WillDestroyApplication(application));
  applications_.erase(found);
  if (applications_.empty()) {
    base::MessageLoop::current()->PostTask(
            FROM_HERE, base::MessageLoop::QuitClosure());
  }
}

Application* ApplicationService::Launch(
    scoped_refptr<ApplicationData> application_data,
    Application::LaunchEntryPoints entry_points) {
  event_manager_->OnAppLoaded(application_data->ID());

  Application* application(new Application(application_data,
                                           runtime_context_,
                                           this));
  application->set_entry_points(entry_points);
  applications_.push_back(application);
  if (!application->Launch()) {
    applications_.get().pop_back();
    delete application;
    application = NULL;
  } else {
    FOR_EACH_OBSERVER(Observer, observers_,
                      DidLaunchApplication(application));
  }

  return application;
}

void ApplicationService::CheckAPIAccessControl(const std::string& app_id,
    const std::string& extension_name,
    const std::string& api_name, const PermissionCallback& callback) {
  Application* app = GetApplicationByID(app_id);
  if (!app) {
    LOG(ERROR) << "No running application found with ID: "
      << app_id;
    callback.Run(INVALID_RUNTIME_PERM);
    return;
  }
  if (!app->ContainsExtension(extension_name)) {
    LOG(ERROR) << "Can not find extension: "
      << extension_name << " of Application with ID: "
      << app_id;
    callback.Run(INVALID_RUNTIME_PERM);
    return;
  }
  // Permission name should have been resigtered at extension initialization.
  std::string permission_name =
      app->GetRegisteredPermissionName(extension_name, api_name);
  if (permission_name.size() == 0) {
    LOG(ERROR) << "API: " << api_name << " of extension: "
      << extension_name << " not registered!";
    callback.Run(INVALID_RUNTIME_PERM);
    return;
  }
  // Okay, since we have the permission name, let's get down to the policies.
  // First, find out whether the permission is stored for the current session.
  StoredPermission perm = app->GetPermission(
      SESSION_PERMISSION, permission_name);
  if (perm != INVALID_STORED_PERM) {
    // "PROMPT" should not be in the session storage.
    DCHECK(perm != PROMPT);
    if (perm == ALLOW) {
      callback.Run(ALLOW_SESSION);
      return;
    }
    if (perm == DENY) {
      callback.Run(DENY_SESSION);
      return;
    }
    NOTREACHED();
  }
  // Then, consult the persistent policy storage.
  perm = app->GetPermission(PERSISTENT_PERMISSION, permission_name);
  // Permission not found in persistent permission table, normally this should
  // not happen because all the permission needed by the application should be
  // contained in its manifest, so it also means that the application is asking
  // for something wasn't allowed.
  if (perm == INVALID_STORED_PERM) {
    callback.Run(INVALID_RUNTIME_PERM);
    return;
  }
  if (perm == PROMPT) {
    // TODO(Bai): We needed to pop-up a dialog asking user to chose one from
    // either allow/deny for session/one shot/forever. Then, we need to update
    // the session and persistent policy accordingly.
    callback.Run(INVALID_RUNTIME_PERM);
    return;
  }
  if (perm == ALLOW) {
    callback.Run(ALLOW_FOREVER);
    return;
  }
  if (perm == DENY) {
    callback.Run(DENY_FOREVER);
    return;
  }
  NOTREACHED();
}

bool ApplicationService::RegisterPermissions(const std::string& app_id,
    const std::string& extension_name,
    const std::string& perm_table) {
  Application* app = GetApplicationByID(app_id);
  if (!app) {
    LOG(ERROR) << "No running application found with ID: "
      << app_id;
    return false;
  }
  if (!app->ContainsExtension(extension_name)) {
    LOG(ERROR) << "Can not find extension: "
      << extension_name << " of Application with ID: "
      << app_id;
    return false;
  }
  return app->RegisterPermissions(extension_name, perm_table);
}

}  // namespace application
}  // namespace xwalk
