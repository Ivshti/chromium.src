// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/dom_distiller/core/dom_distiller_store.h"

#include "base/bind.h"
#include "base/logging.h"
#include "components/dom_distiller/core/article_entry.h"
#include "sync/api/sync_change.h"
#include "sync/protocol/article_specifics.pb.h"
#include "sync/protocol/sync.pb.h"

using sync_pb::ArticleSpecifics;
using sync_pb::EntitySpecifics;
using syncer::ModelType;
using syncer::SyncChange;
using syncer::SyncChangeList;
using syncer::SyncData;
using syncer::SyncDataList;
using syncer::SyncError;
using syncer::SyncMergeResult;

namespace dom_distiller {

DomDistillerStore::DomDistillerStore(
    scoped_ptr<DomDistillerDatabaseInterface> database,
    const base::FilePath& database_dir)
    : database_(database.Pass()),
      database_loaded_(false),
      weak_ptr_factory_(this) {
  database_->Init(database_dir,
                  base::Bind(&DomDistillerStore::OnDatabaseInit,
                             weak_ptr_factory_.GetWeakPtr()));
}

DomDistillerStore::DomDistillerStore(
    scoped_ptr<DomDistillerDatabaseInterface> database,
    const std::vector<ArticleEntry>& initial_data,
    const base::FilePath& database_dir)
    : database_(database.Pass()),
      database_loaded_(false),
      model_(initial_data),
      weak_ptr_factory_(this) {
  database_->Init(database_dir,
                  base::Bind(&DomDistillerStore::OnDatabaseInit,
                             weak_ptr_factory_.GetWeakPtr()));
}

DomDistillerStore::~DomDistillerStore() {}

// DomDistillerStoreInterface implementation.
syncer::SyncableService* DomDistillerStore::GetSyncableService() {
  return this;
}

bool DomDistillerStore::GetEntryById(const std::string& entry_id,
                                     ArticleEntry* entry) {
  return model_.GetEntryById(entry_id, entry);
}

bool DomDistillerStore::GetEntryByUrl(const GURL& url,
                                     ArticleEntry* entry) {
  return model_.GetEntryByUrl(url, entry);
}


bool DomDistillerStore::AddEntry(const ArticleEntry& entry) {
  if (!database_loaded_) {
    return false;
  }

  if (model_.GetEntryById(entry.entry_id(), NULL)) {
    return false;
  }

  SyncChangeList changes_to_apply;
  changes_to_apply.push_back(
      SyncChange(FROM_HERE, SyncChange::ACTION_ADD, CreateLocalData(entry)));

  SyncChangeList changes_applied;
  SyncChangeList changes_missing;

  if (!ApplyChangesToModel(
           changes_to_apply, &changes_applied, &changes_missing)) {
    return false;
  }

  DCHECK_EQ(size_t(0), changes_missing.size());
  DCHECK_EQ(size_t(1), changes_applied.size());

  ApplyChangesToSync(FROM_HERE, changes_applied);
  ApplyChangesToDatabase(changes_applied);

  return true;
}

std::vector<ArticleEntry> DomDistillerStore::GetEntries() const {
  return model_.GetEntries();
}

// syncer::SyncableService implementation.
SyncMergeResult DomDistillerStore::MergeDataAndStartSyncing(
    ModelType type,
    const SyncDataList& initial_sync_data,
    scoped_ptr<syncer::SyncChangeProcessor> sync_processor,
    scoped_ptr<syncer::SyncErrorFactory> error_handler) {
  DCHECK_EQ(syncer::ARTICLES, type);
  DCHECK(!sync_processor_);
  DCHECK(!error_factory_);
  sync_processor_.reset(sync_processor.release());
  error_factory_.reset(error_handler.release());

  SyncChangeList database_changes;
  SyncChangeList sync_changes;
  SyncMergeResult result =
      MergeDataWithModel(initial_sync_data, &database_changes, &sync_changes);
  ApplyChangesToDatabase(database_changes);
  ApplyChangesToSync(FROM_HERE, sync_changes);

  return result;
}

void DomDistillerStore::StopSyncing(ModelType type) {
  sync_processor_.reset();
  error_factory_.reset();
}

SyncDataList DomDistillerStore::GetAllSyncData(ModelType type) const {
  return model_.GetAllSyncData();
}

SyncError DomDistillerStore::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const SyncChangeList& change_list) {
  DCHECK(database_loaded_);
  SyncChangeList database_changes;
  SyncChangeList sync_changes;
  if (!ApplyChangesToModel(change_list, &database_changes, &sync_changes)) {
    return SyncError(FROM_HERE,
                     SyncError::DATATYPE_ERROR,
                     "Applying changes to the DOM distiller model failed",
                     syncer::ARTICLES);
  }
  ApplyChangesToDatabase(database_changes);
  DCHECK_EQ(size_t(0), sync_changes.size());
  return SyncError();
}

bool DomDistillerStore::ApplyChangesToModel(
    const SyncChangeList& changes,
    SyncChangeList* changes_applied,
    SyncChangeList* changes_missing) {
  DomDistillerModel::ChangeResult change_result =
      model_.ApplyChangesToModel(changes, changes_applied, changes_missing);
  if (change_result == DomDistillerModel::SUCCESS) {
    return true;
  }

  LOG(WARNING) << "Applying changes to DOM distiller model failed with error "
               << change_result;

  database_.reset();
  database_loaded_ = false;
  StopSyncing(syncer::ARTICLES);
  return false;
}

void DomDistillerStore::OnDatabaseInit(bool success) {
  if (!success) {
    LOG(INFO) << "DOM Distiller database init failed.";
    database_.reset();
    return;
  }
  database_->LoadEntries(base::Bind(&DomDistillerStore::OnDatabaseLoad,
                                    weak_ptr_factory_.GetWeakPtr()));
}

void DomDistillerStore::OnDatabaseLoad(bool success,
                                       scoped_ptr<EntryVector> entries) {
  if (!success) {
    LOG(INFO) << "DOM Distiller database load failed.";
    database_.reset();
    return;
  }
  database_loaded_ = true;

  SyncDataList data;
  for (EntryVector::iterator it = entries->begin(); it != entries->end();
       ++it) {
    data.push_back(CreateLocalData(*it));
  }
  SyncChangeList changes_applied;
  SyncChangeList database_changes_needed;
  MergeDataWithModel(data, &changes_applied, &database_changes_needed);
  ApplyChangesToDatabase(database_changes_needed);
}

void DomDistillerStore::OnDatabaseSave(bool success) {
  if (!success) {
    LOG(INFO) << "DOM Distiller database save failed."
              << " Disabling modifications and sync.";
    database_.reset();
    database_loaded_ = false;
    StopSyncing(syncer::ARTICLES);
  }
}

bool DomDistillerStore::ApplyChangesToSync(
    const tracked_objects::Location& from_here,
    const SyncChangeList& change_list) {
  if (!sync_processor_) {
    return false;
  }
  if (change_list.empty()) {
    return true;
  }

  SyncError error = sync_processor_->ProcessSyncChanges(from_here, change_list);
  if (error.IsSet()) {
    StopSyncing(syncer::ARTICLES);
    return false;
  }
  return true;
}

bool DomDistillerStore::ApplyChangesToDatabase(
    const SyncChangeList& change_list) {
  if (!database_loaded_) {
    return false;
  }
  if (change_list.empty()) {
    return true;
  }
  scoped_ptr<EntryVector> entries_to_save(new EntryVector());

  for (SyncChangeList::const_iterator it = change_list.begin();
       it != change_list.end();
       ++it) {
    entries_to_save->push_back(GetEntryFromChange(*it));
  }
  database_->SaveEntries(entries_to_save.Pass(),
                         base::Bind(&DomDistillerStore::OnDatabaseSave,
                                    weak_ptr_factory_.GetWeakPtr()));
  return true;
}

SyncMergeResult DomDistillerStore::MergeDataWithModel(
    const SyncDataList& data,
    SyncChangeList* changes_applied,
    SyncChangeList* changes_missing) {
  DCHECK(changes_applied);
  DCHECK(changes_missing);

  SyncMergeResult result(syncer::ARTICLES);
  result.set_num_items_before_association(model_.GetNumEntries());

  SyncChangeList changes_to_apply;
  model_.CalculateChangesForMerge(data, &changes_to_apply, changes_missing);
  SyncError error;
  if (!ApplyChangesToModel(
           changes_to_apply, changes_applied, changes_missing)) {
    error = SyncError(FROM_HERE,
                      SyncError::DATATYPE_ERROR,
                      "Applying changes to the DOM distiller model failed",
                      syncer::ARTICLES);
  }

  int num_added = 0;
  int num_modified = 0;
  for (SyncChangeList::const_iterator it = changes_applied->begin();
       it != changes_applied->end();
       ++it) {
    DCHECK(it->IsValid());
    switch (it->change_type()) {
      case SyncChange::ACTION_ADD:
        num_added++;
        break;
      case SyncChange::ACTION_UPDATE:
        num_modified++;
        break;
      default:
        NOTREACHED();
    }
  }
  result.set_num_items_added(num_added);
  result.set_num_items_modified(num_modified);
  result.set_num_items_deleted(0);

  result.set_pre_association_version(0);
  result.set_num_items_after_association(model_.GetNumEntries());
  result.set_error(error);

  return result;
}

}  // namespace dom_distiller
