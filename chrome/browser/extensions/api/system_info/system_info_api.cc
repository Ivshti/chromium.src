// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/system_info/system_info_api.h"

#include <set>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/api/system_info_storage/storage_info_provider.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/event_router_forwarder.h"
#include "chrome/browser/storage_monitor/removable_storage_observer.h"
#include "chrome/browser/storage_monitor/storage_info.h"
#include "chrome/browser/storage_monitor/storage_monitor.h"
#include "chrome/common/extensions/api/experimental_system_info_storage.h"
#include "ui/gfx/display_observer.h"

#if defined(USE_ASH)
#include "ash/screen_ash.h"
#include "ash/shell.h"
#endif

namespace extensions {

using api::experimental_system_info_storage::StorageFreeSpaceChangeInfo;
using api::experimental_system_info_storage::StorageUnitInfo;
using api::experimental_system_info_storage::StorageUnitType;
using content::BrowserThread;

namespace {

// The display events use the "systemInfo" prefix.
const char kSystemInfoEventPrefix[] = "systemInfo";
// The storage events still use the "experimental.systemInfo" prefix.
const char kExperimentalSystemInfoEventPrefix[] = "experimental.systemInfo";

bool IsDisplayChangedEvent(const std::string& event_name) {
  return event_name == event_names::kOnDisplayChanged;
}

bool IsAvailableCapacityChangedEvent(const std::string& event_name) {
  return event_name == event_names::kOnStorageAvailableCapacityChanged;
}

// Event router for systemInfo API. It is a singleton instance shared by
// multiple profiles.
class SystemInfoEventRouter : public gfx::DisplayObserver,
                              public StorageFreeSpaceObserver,
                              public chrome::RemovableStorageObserver {
 public:
  static SystemInfoEventRouter* GetInstance();

  SystemInfoEventRouter();
  virtual ~SystemInfoEventRouter();

  // Add/remove event listener for the |event_name| event.
  void AddEventListener(const std::string& event_name);
  void RemoveEventListener(const std::string& event_name);

  // Return true if the |event_name| is an event from systemInfo namespace.
  static bool IsSystemInfoEvent(const std::string& event_name);

 private:
  // StorageFreeSpaceObserver:
  virtual void OnFreeSpaceChanged(const std::string& id,
                                  double new_value,
                                  double old_value) OVERRIDE;

  // gfx::DisplayObserver:
  virtual void OnDisplayBoundsChanged(const gfx::Display& display) OVERRIDE;
  virtual void OnDisplayAdded(const gfx::Display& new_display) OVERRIDE;
  virtual void OnDisplayRemoved(const gfx::Display& old_display) OVERRIDE;

  // chrome::RemovableStorageObserver implementation.
  virtual void OnRemovableStorageAttached(
      const chrome::StorageInfo& info) OVERRIDE;
  virtual void OnRemovableStorageDetached(
      const chrome::StorageInfo& info) OVERRIDE;

  void DispatchStorageAttachedEvent(const chrome::StorageInfo& info,
                                    int64 avail_bytes);

  // Called from any thread to dispatch the systemInfo event to all extension
  // processes cross multiple profiles.
  void DispatchEvent(const std::string& event_name,
      scoped_ptr<base::ListValue> args);

  // The callbacks of querying storage info to start and stop watching the
  // storages. Called from UI thread.
  void StartWatchingStorages(const StorageInfo& info, bool success);
  void StopWatchingStorages(const StorageInfo& info, bool success);

  // Called to dispatch the systemInfo.display.onDisplayChanged event.
  void OnDisplayChanged();

  // Used to record the event names being watched.
  std::multiset<std::string> watching_event_set_;

  DISALLOW_COPY_AND_ASSIGN(SystemInfoEventRouter);
};

static base::LazyInstance<SystemInfoEventRouter>::Leaky
  g_system_info_event_router = LAZY_INSTANCE_INITIALIZER;

// static
SystemInfoEventRouter* SystemInfoEventRouter::GetInstance() {
  return g_system_info_event_router.Pointer();
}

SystemInfoEventRouter::SystemInfoEventRouter() {
  StorageInfoProvider::Get()->AddObserver(this);
  chrome::StorageMonitor::GetInstance()->AddObserver(this);
}

SystemInfoEventRouter::~SystemInfoEventRouter() {
  StorageInfoProvider::Get()->RemoveObserver(this);
  if (chrome::StorageMonitor* storage_monitor =
          chrome::StorageMonitor::GetInstance())
    storage_monitor->RemoveObserver(this);
}

void SystemInfoEventRouter::StartWatchingStorages(
    const StorageInfo& info, bool success) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!success)
    return;

  for (StorageInfo::const_iterator it = info.begin(); it != info.end(); ++it) {
    StorageInfoProvider::Get()->StartWatching((*it)->id);
  }
}

void SystemInfoEventRouter::StopWatchingStorages(
    const StorageInfo& info, bool success) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!success)
    return;

  for (StorageInfo::const_iterator it = info.begin(); it != info.end(); ++it) {
    StorageInfoProvider::Get()->StopWatching((*it)->id);
  }
}

void SystemInfoEventRouter::AddEventListener(const std::string& event_name) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  watching_event_set_.insert(event_name);
  if (watching_event_set_.count(event_name) > 1)
    return;

  // Start watching the |event_name| event if the first event listener arrives.
  // For systemInfo.storage event.
  if (IsAvailableCapacityChangedEvent(event_name)) {
    StorageInfoProvider::Get()->StartQueryInfo(
        base::Bind(&SystemInfoEventRouter::StartWatchingStorages,
                   base::Unretained(this)));
    return;
  }

  // For systemInfo.display event.
  if (IsDisplayChangedEvent(event_name)) {
#if defined(USE_ASH)
    ash::Shell::GetScreen()->AddObserver(this);
#endif
  }
}

void SystemInfoEventRouter::RemoveEventListener(
    const std::string& event_name) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  std::multiset<std::string>::iterator it =
      watching_event_set_.find(event_name);
  if (it != watching_event_set_.end())
    watching_event_set_.erase(it);

  if (watching_event_set_.count(event_name) > 0)
    return;

  // In case of the last event listener is removed, we need to stop watching
  // it to avoid unnecessary overhead.
  if (IsAvailableCapacityChangedEvent(event_name)) {
    StorageInfoProvider::Get()->StartQueryInfo(
        base::Bind(&SystemInfoEventRouter::StopWatchingStorages,
                   base::Unretained(this)));
  }

  if (IsDisplayChangedEvent(event_name)) {
#if defined(USE_ASH)
    ash::Shell::GetScreen()->RemoveObserver(this);
#endif
  }
}

// static
bool SystemInfoEventRouter::IsSystemInfoEvent(const std::string& event_name) {
  // TODO(hshi): simplify this once all systemInfo APIs are out of experimental.
  return (StartsWithASCII(event_name, kSystemInfoEventPrefix, true) ||
          StartsWithASCII(event_name, kExperimentalSystemInfoEventPrefix,
                          true));
}

// Called on UI thread since the observer is added from UI thread.
void SystemInfoEventRouter::OnFreeSpaceChanged(
    const std::string& transient_id, double new_value, double old_value) {
  StorageFreeSpaceChangeInfo info;
  info.id = transient_id;
  info.available_capacity = static_cast<double>(new_value);

  scoped_ptr<base::ListValue> args(new base::ListValue());
  args->Append(info.ToValue().release());

  DispatchEvent(event_names::kOnStorageAvailableCapacityChanged, args.Pass());
}

void SystemInfoEventRouter::OnRemovableStorageAttached(
    const chrome::StorageInfo& info) {
  base::PostTaskAndReplyWithResult(
      BrowserThread::GetBlockingPool()->GetTaskRunnerWithShutdownBehavior(
          base::SequencedWorkerPool::CONTINUE_ON_SHUTDOWN),
      FROM_HERE,
      base::Bind(&StorageInfoProvider::GetStorageFreeSpaceFromTransientId,
                 StorageInfoProvider::Get(),
                 StorageInfoProvider::Get()->GetTransientIdForDeviceId(
                     info.device_id())),
      base::Bind(&SystemInfoEventRouter::DispatchStorageAttachedEvent,
                 // Since SystemInfoEventRouter is a global lazy instance, this
                 // pointer will be alive when the reply comes back.
                 base::Unretained(this),
                 info));
}

void SystemInfoEventRouter::DispatchStorageAttachedEvent(
    const chrome::StorageInfo& info, int64 avail_bytes) {
  StorageUnitInfo unit;
  systeminfo::BuildStorageUnitInfo(info, &unit);

  unit.available_capacity =
      avail_bytes > 0 ? static_cast<double>(avail_bytes) : 0;

  scoped_ptr<base::ListValue> args(new base::ListValue);
  args->Append(unit.ToValue().release());
  DispatchEvent(event_names::kOnStorageAttached, args.Pass());
}

void SystemInfoEventRouter::OnRemovableStorageDetached(
    const chrome::StorageInfo& info) {
  scoped_ptr<base::ListValue> args(new base::ListValue);
  args->Append(new base::StringValue(StorageInfoProvider::Get()->
                   GetTransientIdForDeviceId(info.device_id())));

  DispatchEvent(event_names::kOnStorageDetached, args.Pass());
}

void SystemInfoEventRouter::OnDisplayBoundsChanged(
    const gfx::Display& display) {
  OnDisplayChanged();
}

void SystemInfoEventRouter::OnDisplayAdded(const gfx::Display& new_display) {
  OnDisplayChanged();
}

void SystemInfoEventRouter::OnDisplayRemoved(const gfx::Display& old_display) {
  OnDisplayChanged();
}

void SystemInfoEventRouter::OnDisplayChanged() {
  scoped_ptr<base::ListValue> args(new base::ListValue());
  DispatchEvent(event_names::kOnDisplayChanged, args.Pass());
}

void SystemInfoEventRouter::DispatchEvent(const std::string& event_name,
                                          scoped_ptr<base::ListValue> args) {
  g_browser_process->extension_event_router_forwarder()->
      BroadcastEventToRenderers(event_name, args.Pass(), GURL());
}

}  // namespace

static base::LazyInstance<ProfileKeyedAPIFactory<SystemInfoAPI> >
    g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<SystemInfoAPI>* SystemInfoAPI::GetFactoryInstance() {
  return &g_factory.Get();
}

SystemInfoAPI::SystemInfoAPI(Profile* profile) : profile_(profile) {
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, event_names::kOnStorageAvailableCapacityChanged);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, event_names::kOnStorageAttached);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, event_names::kOnStorageDetached);
}

SystemInfoAPI::~SystemInfoAPI() {
}

void SystemInfoAPI::Shutdown() {
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
}

void SystemInfoAPI::OnListenerAdded(const EventListenerInfo& details) {
  SystemInfoEventRouter::GetInstance()->AddEventListener(details.event_name);
}

void SystemInfoAPI::OnListenerRemoved(const EventListenerInfo& details) {
  SystemInfoEventRouter::GetInstance()->RemoveEventListener(details.event_name);
}

}  // namespace extensions
