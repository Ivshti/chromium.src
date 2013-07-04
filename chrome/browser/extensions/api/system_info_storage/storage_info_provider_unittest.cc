// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// StorageInfoProvider unit tests.

#include "base/message_loop.h"
#include "base/stl_util.h"
#include "chrome/browser/extensions/api/system_info_storage/storage_info_provider.h"
#include "chrome/browser/extensions/api/system_info_storage/test_storage_info_provider.h"
#include "chrome/browser/storage_monitor/test_storage_monitor.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

using api::experimental_system_info_storage::ParseStorageUnitType;
using api::experimental_system_info_storage::StorageUnitInfo;
using api::experimental_system_info_storage::StorageUnitType;
using base::MessageLoop;
using chrome::test::TestStorageMonitor;
using content::BrowserThread;
using content::RunAllPendingInMessageLoop;
using content::RunMessageLoop;
using testing::Return;
using testing::_;

const struct TestStorageUnitInfo kTestingData[] = {
  {"device:001", "transient:01", "C:", systeminfo::kStorageTypeUnknown,
    1000, 10, 0},
  {"device:002", "transient:02", "d:", systeminfo::kStorageTypeRemovable,
    2000, 10, 1},
  {"device:003", "transient:03", "/home", systeminfo::kStorageTypeFixed,
    3000, 10, 2},
  {"device:004", "transient:04", "/", systeminfo::kStorageTypeRemovable,
    4000, 10, 3}
};

// The watching interval for unit test is 1 milliseconds.
const size_t kWatchingIntervalMs = 1u;
// The number of times of checking watched storages.
const int kCheckTimes = 10;

class MockStorageObserver : public StorageFreeSpaceObserver {
 public:
  MockStorageObserver() {}
  virtual ~MockStorageObserver() {}

  MOCK_METHOD3(OnFreeSpaceChanged, void(const std::string&,
     double, double));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockStorageObserver);
};

const int kMaxCheckWatchStorageTimes = 10;

// A testing observer used to provide the statistics of how many times
// that the storage free space has been changed and check the change against
// our expectation.
class TestStorageObserver : public StorageFreeSpaceObserver {
 public:
  TestStorageObserver() : change_times_(0) {
    for (size_t i = 0; i < arraysize(kTestingData); ++i)
      testing_data_.push_back(kTestingData[i]);
  }

  virtual ~TestStorageObserver() {}

  virtual void OnFreeSpaceChanged(const std::string& transient_id,
                                  double old_value,
                                  double new_value) OVERRIDE {
    // The observer is added on UI thread, so the callback should be also
    // called on UI thread.
    ASSERT_TRUE(BrowserThread::CurrentlyOn(BrowserThread::UI));
    size_t i = 0;
    for (; i < testing_data_.size(); ++i) {
      if (testing_data_[i].transient_id == transient_id) {
        EXPECT_DOUBLE_EQ(new_value-old_value, testing_data_[i].change_step);
        ++change_times_;
        break;
      }
    }

    ASSERT_TRUE(i != testing_data_.size());
  }

 private:
  // A copy of |kTestingData|.
  std::vector<TestStorageUnitInfo> testing_data_;
  int change_times_;
};

class UnitTestStorageInfoProvider : public TestStorageInfoProvider {
 public :
  UnitTestStorageInfoProvider(const struct TestStorageUnitInfo* testing_data,
                              size_t n)
      : TestStorageInfoProvider(testing_data, n),
        check_watch_storage_times_(0) {}

 protected:
  virtual ~UnitTestStorageInfoProvider() {}

 private:
  virtual int64 GetStorageFreeSpaceFromTransientId(
      const std::string& transient_id) OVERRIDE {
    int64 available_capacity = -1;
    for (size_t i = 0; i < testing_data_.size(); ++i) {
      if (testing_data_[i].transient_id == transient_id) {
        available_capacity = testing_data_[i].available_capacity;
        // We simulate free space change by increasing the |available_capacity|
        // with a fixed change step.
        testing_data_[i].available_capacity += testing_data_[i].change_step;

        // Add up the counting variable.
        ++check_watch_storage_times_;

        // Post a quit task to UI thread for test result verification
        // if |check_watch_storage_times_| is greater than the threshold value
        // |kMaxCheckWatchStorageTimes|.
        if (check_watch_storage_times_ > kMaxCheckWatchStorageTimes) {
          BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                MessageLoop::QuitClosure());
          check_watch_storage_times_ = 0;
        }

        break;
      }
    }
    return available_capacity;
  }

  int check_watch_storage_times_;
};

class StorageInfoProviderTest : public testing::Test {
 public:
  StorageInfoProviderTest();
  virtual ~StorageInfoProviderTest();

 protected:
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  // Run message loop and flush blocking pool to make sure there is no pending
  // tasks on blocking pool.
  static void RunLoopAndFlushBlockingPool();
  static void RunAllPendingAndFlushBlockingPool();

  base::MessageLoop message_loop_;
  content::TestBrowserThread ui_thread_;
  scoped_refptr<UnitTestStorageInfoProvider> storage_info_provider_;
  scoped_ptr<TestStorageMonitor> storage_test_notifications_;
};

StorageInfoProviderTest::StorageInfoProviderTest()
    : message_loop_(base::MessageLoop::TYPE_UI),
      ui_thread_(BrowserThread::UI, &message_loop_) {
}

StorageInfoProviderTest::~StorageInfoProviderTest() {
}

void StorageInfoProviderTest::SetUp() {
  ASSERT_TRUE(BrowserThread::CurrentlyOn(BrowserThread::UI));
  storage_test_notifications_.reset(new TestStorageMonitor);
  storage_info_provider_ = new UnitTestStorageInfoProvider(
      kTestingData, arraysize(kTestingData));
}

void StorageInfoProviderTest::TearDown() {
  RunAllPendingAndFlushBlockingPool();
}

void StorageInfoProviderTest::RunLoopAndFlushBlockingPool() {
  RunMessageLoop();
  content::BrowserThread::GetBlockingPool()->FlushForTesting();
}

void StorageInfoProviderTest::RunAllPendingAndFlushBlockingPool() {
  RunAllPendingInMessageLoop();
  content::BrowserThread::GetBlockingPool()->FlushForTesting();
}

TEST_F(StorageInfoProviderTest, WatchingNoChangedStorage) {
  // Case 1: watching a storage that the free space is not changed.
  MockStorageObserver observer;
  storage_info_provider_->AddObserver(&observer);
  storage_info_provider_->StartWatching(kTestingData[0].transient_id);
  EXPECT_CALL(observer, OnFreeSpaceChanged(kTestingData[0].transient_id, _, _))
      .Times(0);

  RunLoopAndFlushBlockingPool();

  storage_info_provider_->RemoveObserver(&observer);
  storage_info_provider_->StopWatching(kTestingData[0].device_id);
  RunAllPendingAndFlushBlockingPool();
}

TEST_F(StorageInfoProviderTest, WatchingOneStorage) {
  // Case 2: only watching one storage.
  TestStorageObserver observer;
  storage_info_provider_->AddObserver(&observer);
  storage_info_provider_->StartWatching(kTestingData[1].transient_id);
  RunLoopAndFlushBlockingPool();

  storage_info_provider_->StopWatching(kTestingData[1].transient_id);
  // Give a chance to run StopWatching task on the blocking pool.
  RunAllPendingAndFlushBlockingPool();

  MockStorageObserver mock_observer;
  storage_info_provider_->AddObserver(&mock_observer);
  // The watched storage won't get free space change notification.
  EXPECT_CALL(mock_observer,
      OnFreeSpaceChanged(kTestingData[1].transient_id, _, _)).Times(0);
  RunAllPendingAndFlushBlockingPool();

  storage_info_provider_->RemoveObserver(&observer);
  storage_info_provider_->RemoveObserver(&mock_observer);
}

TEST_F(StorageInfoProviderTest, WatchingMultipleStorages) {
  // Case 2: watching multiple storages. We ignore the first entry in
  // |kTestingData| since its change_step is zero.
  TestStorageObserver observer;
  storage_info_provider_->AddObserver(&observer);

  for (size_t k = 1; k < arraysize(kTestingData); ++k) {
    storage_info_provider_->StartWatching(kTestingData[k].transient_id);
  }
  RunLoopAndFlushBlockingPool();

  // Stop watching the first storage.
  storage_info_provider_->StopWatching(kTestingData[1].transient_id);
  RunAllPendingAndFlushBlockingPool();

  MockStorageObserver mock_observer;
  storage_info_provider_->AddObserver(&mock_observer);
  for (size_t k = 2; k < arraysize(kTestingData); ++k) {
    EXPECT_CALL(mock_observer,
        OnFreeSpaceChanged(kTestingData[k].transient_id,  _, _))
        .WillRepeatedly(Return());
  }

  // After stopping watching, the observer won't get change notification.
  EXPECT_CALL(mock_observer,
      OnFreeSpaceChanged(kTestingData[1].transient_id, _, _)).Times(0);
  RunLoopAndFlushBlockingPool();

  for (size_t k = 1; k < arraysize(kTestingData); ++k) {
    storage_info_provider_->StopWatching(kTestingData[k].transient_id);
  }
  RunAllPendingAndFlushBlockingPool();
  storage_info_provider_->RemoveObserver(&observer);
  storage_info_provider_->RemoveObserver(&mock_observer);
}

TEST_F(StorageInfoProviderTest, WatchingInvalidStorage) {
  // Case 3: watching an invalid storage.
  std::string invalid_id("invalid_id");
  MockStorageObserver mock_observer;
  storage_info_provider_->AddObserver(&mock_observer);
  storage_info_provider_->StartWatching(invalid_id);
  EXPECT_CALL(mock_observer,
      OnFreeSpaceChanged(invalid_id, _, _)).Times(0);
  RunAllPendingAndFlushBlockingPool();
  storage_info_provider_->RemoveObserver(&mock_observer);
}

} // namespace extensions
