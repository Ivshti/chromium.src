// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/notifier/unacked_invalidation_set.h"

#include "base/json/json_string_value_serializer.h"
#include "sync/notifier/object_id_invalidation_map.h"
#include "sync/notifier/single_object_invalidation_set.h"
#include "testing/gmock/include/gmock/gmock-matchers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

// Start with some helper functions and classes.

using ::testing::MakeMatcher;
using ::testing::MatchResultListener;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::PrintToString;

void PrintTo(
    const UnackedInvalidationSet& invalidations, ::std::ostream* os);

void PrintTo(
    const UnackedInvalidationsMap& map, ::std::ostream* os);

::testing::Matcher<const UnackedInvalidationSet&> Eq(
    const UnackedInvalidationSet& expected);

::testing::Matcher<const UnackedInvalidationsMap&> Eq(
    const UnackedInvalidationsMap& expected);

class UnackedInvalidationSetEqMatcher
    : public testing::MatcherInterface<const UnackedInvalidationSet&> {
 public:
  explicit UnackedInvalidationSetEqMatcher(
      const UnackedInvalidationSet& expected);

  virtual bool MatchAndExplain(
      const UnackedInvalidationSet& actual,
      MatchResultListener* listener) const OVERRIDE;
  virtual void DescribeTo(::std::ostream* os) const OVERRIDE;
  virtual void DescribeNegationTo(::std::ostream* os) const OVERRIDE;

 private:
  const UnackedInvalidationSet expected_;

  DISALLOW_COPY_AND_ASSIGN(UnackedInvalidationSetEqMatcher);
};

UnackedInvalidationSetEqMatcher::UnackedInvalidationSetEqMatcher(
    const UnackedInvalidationSet& expected)
  : expected_(expected) {}

namespace {

struct InvalidationEq {
  bool operator()(const syncer::Invalidation& a,
                  const syncer::Invalidation& b) const {
    return a.Equals(b);
  }
};

}  // namespace

bool UnackedInvalidationSetEqMatcher::MatchAndExplain(
    const UnackedInvalidationSet& actual,
    MatchResultListener* listener) const {
  // Use our friendship with this class to compare the internals of two
  // instances.
  //
  // Note that the registration status is intentionally not considered
  // when performing this comparison.
  return expected_.object_id_ == actual.object_id_
      && std::equal(expected_.invalidations_.begin(),
                    expected_.invalidations_.end(),
                    actual.invalidations_.begin(),
                    InvalidationEq());
}

void UnackedInvalidationSetEqMatcher::DescribeTo(::std::ostream* os) const {
  *os << " is equal to " << PrintToString(expected_);
}

void UnackedInvalidationSetEqMatcher::DescribeNegationTo(
    ::std::ostream* os) const {
  *os << " isn't equal to " << PrintToString(expected_);
}

namespace {

ObjectIdInvalidationMap UnackedInvalidationsMapToObjectIdInvalidationMap(
    const UnackedInvalidationsMap& state_map) {
  ObjectIdInvalidationMap object_id_invalidation_map;
  for (UnackedInvalidationsMap::const_iterator it = state_map.begin();
       it != state_map.end(); ++it) {
    it->second.ExportInvalidations(syncer::WeakHandle<AckHandler>(),
                                   &object_id_invalidation_map);
  }
  return object_id_invalidation_map;
}

class UnackedInvalidationsMapEqMatcher
    : public testing::MatcherInterface<const UnackedInvalidationsMap&> {
 public:
  explicit UnackedInvalidationsMapEqMatcher(
      const UnackedInvalidationsMap& expected);

  virtual bool MatchAndExplain(const UnackedInvalidationsMap& actual,
                               MatchResultListener* listener) const;
  virtual void DescribeTo(::std::ostream* os) const;
  virtual void DescribeNegationTo(::std::ostream* os) const;

 private:
  const UnackedInvalidationsMap expected_;

  DISALLOW_COPY_AND_ASSIGN(UnackedInvalidationsMapEqMatcher);
};

UnackedInvalidationsMapEqMatcher::UnackedInvalidationsMapEqMatcher(
    const UnackedInvalidationsMap& expected)
    : expected_(expected) {
}

bool UnackedInvalidationsMapEqMatcher::MatchAndExplain(
    const UnackedInvalidationsMap& actual,
    MatchResultListener* listener) const {
  ObjectIdInvalidationMap expected_inv =
      UnackedInvalidationsMapToObjectIdInvalidationMap(expected_);
  ObjectIdInvalidationMap actual_inv =
      UnackedInvalidationsMapToObjectIdInvalidationMap(actual);

  return expected_inv == actual_inv;
}

void UnackedInvalidationsMapEqMatcher::DescribeTo(
    ::std::ostream* os) const {
  *os << " is equal to " << PrintToString(expected_);
}

void UnackedInvalidationsMapEqMatcher::DescribeNegationTo(
    ::std::ostream* os) const {
  *os << " isn't equal to " << PrintToString(expected_);
}

}  // namespace

void PrintTo(const UnackedInvalidationSet& invalidations,
             ::std::ostream* os) {
  scoped_ptr<base::DictionaryValue> value = invalidations.ToValue();

  std::string output;
  JSONStringValueSerializer serializer(&output);
  serializer.set_pretty_print(true);
  serializer.Serialize(*value.get());

  (*os) << output;
}

void PrintTo(const UnackedInvalidationsMap& map, ::std::ostream* os) {
  scoped_ptr<base::ListValue> list(new base::ListValue);
  for (UnackedInvalidationsMap::const_iterator it = map.begin();
       it != map.end(); ++it) {
    list->Append(it->second.ToValue().release());
  }

  std::string output;
  JSONStringValueSerializer serializer(&output);
  serializer.set_pretty_print(true);
  serializer.Serialize(*list.get());

  (*os) << output;
}

Matcher<const UnackedInvalidationSet&> Eq(
    const UnackedInvalidationSet& expected) {
  return MakeMatcher(new UnackedInvalidationSetEqMatcher(expected));
}

Matcher<const UnackedInvalidationsMap&> Eq(
    const UnackedInvalidationsMap& expected) {
  return MakeMatcher(new UnackedInvalidationsMapEqMatcher(expected));
}

class UnackedInvalidationSetTest : public testing::Test {
 public:
  UnackedInvalidationSetTest()
      : kObjectId_(10, "ASDF"),
        unacked_invalidations_(kObjectId_) {}

  SingleObjectInvalidationSet GetStoredInvalidations() {
    ObjectIdInvalidationMap map;
    unacked_invalidations_.ExportInvalidations(WeakHandle<AckHandler>(), &map);
    ObjectIdSet ids = map.GetObjectIds();
    if (ids.find(kObjectId_) != ids.end()) {
      return map.ForObject(kObjectId_);
    } else {
      return SingleObjectInvalidationSet();
    }
  }

  const invalidation::ObjectId kObjectId_;
  UnackedInvalidationSet unacked_invalidations_;
};

namespace {

// Test storage and retrieval of zero invalidations.
TEST_F(UnackedInvalidationSetTest, Empty) {
  EXPECT_EQ(0U, GetStoredInvalidations().GetSize());
}

// Test storage and retrieval of a single invalidation.
TEST_F(UnackedInvalidationSetTest, OneInvalidation) {
  Invalidation inv1 = Invalidation::Init(kObjectId_, 10, "payload");
  unacked_invalidations_.Add(inv1);

  SingleObjectInvalidationSet set = GetStoredInvalidations();
  ASSERT_EQ(1U, set.GetSize());
  EXPECT_FALSE(set.StartsWithUnknownVersion());
}

// Test that calling Clear() returns us to the empty state.
TEST_F(UnackedInvalidationSetTest, Clear) {
  Invalidation inv1 = Invalidation::Init(kObjectId_, 10, "payload");
  unacked_invalidations_.Add(inv1);
  unacked_invalidations_.Clear();

  EXPECT_EQ(0U, GetStoredInvalidations().GetSize());
}

// Test that repeated unknown version invalidations are squashed together.
TEST_F(UnackedInvalidationSetTest, UnknownVersions) {
  Invalidation inv1 = Invalidation::Init(kObjectId_, 10, "payload");
  Invalidation inv2 = Invalidation::InitUnknownVersion(kObjectId_);
  Invalidation inv3 = Invalidation::InitUnknownVersion(kObjectId_);
  unacked_invalidations_.Add(inv1);
  unacked_invalidations_.Add(inv2);
  unacked_invalidations_.Add(inv3);

  SingleObjectInvalidationSet set = GetStoredInvalidations();
  ASSERT_EQ(2U, set.GetSize());
  EXPECT_TRUE(set.StartsWithUnknownVersion());
}

// Tests that no truncation occurs while we're under the limit.
TEST_F(UnackedInvalidationSetTest, NoTruncation) {
  size_t kMax = UnackedInvalidationSet::kMaxBufferedInvalidations;

  for (size_t i = 0; i < kMax; ++i) {
    Invalidation inv = Invalidation::Init(kObjectId_, i, "payload");
    unacked_invalidations_.Add(inv);
  }

  SingleObjectInvalidationSet set = GetStoredInvalidations();
  ASSERT_EQ(kMax, set.GetSize());
  EXPECT_FALSE(set.StartsWithUnknownVersion());
  EXPECT_EQ(0, set.begin()->version());
  EXPECT_EQ(kMax-1, static_cast<size_t>(set.rbegin()->version()));
}

// Test that truncation happens as we reach the limit.
TEST_F(UnackedInvalidationSetTest, Truncation) {
  size_t kMax = UnackedInvalidationSet::kMaxBufferedInvalidations;

  for (size_t i = 0; i < kMax + 1; ++i) {
    Invalidation inv = Invalidation::Init(kObjectId_, i, "payload");
    unacked_invalidations_.Add(inv);
  }

  SingleObjectInvalidationSet set = GetStoredInvalidations();
  ASSERT_EQ(kMax, set.GetSize());
  EXPECT_TRUE(set.StartsWithUnknownVersion());
  EXPECT_TRUE(set.begin()->is_unknown_version());
  EXPECT_EQ(kMax, static_cast<size_t>(set.rbegin()->version()));
}

// Test that we don't truncate while a handler is registered.
TEST_F(UnackedInvalidationSetTest, RegistrationAndTruncation) {
  unacked_invalidations_.SetHandlerIsRegistered();

  size_t kMax = UnackedInvalidationSet::kMaxBufferedInvalidations;

  for (size_t i = 0; i < kMax + 1; ++i) {
    Invalidation inv = Invalidation::Init(kObjectId_, i, "payload");
    unacked_invalidations_.Add(inv);
  }

  SingleObjectInvalidationSet set = GetStoredInvalidations();
  ASSERT_EQ(kMax+1, set.GetSize());
  EXPECT_FALSE(set.StartsWithUnknownVersion());
  EXPECT_EQ(0, set.begin()->version());
  EXPECT_EQ(kMax, static_cast<size_t>(set.rbegin()->version()));

  // Unregistering should re-enable truncation.
  unacked_invalidations_.SetHandlerIsUnregistered();
  SingleObjectInvalidationSet set2 = GetStoredInvalidations();
  ASSERT_EQ(kMax, set2.GetSize());
  EXPECT_TRUE(set2.StartsWithUnknownVersion());
  EXPECT_TRUE(set2.begin()->is_unknown_version());
  EXPECT_EQ(kMax, static_cast<size_t>(set2.rbegin()->version()));
}

// Test acknowledgement.
TEST_F(UnackedInvalidationSetTest, Acknowledge) {
  // inv2 is included in this test just to make sure invalidations that
  // are supposed to be unaffected by this operation will be unaffected.

  // We don't expect to be receiving acks or drops unless this flag is set.
  // Not that it makes much of a difference in behavior.
  unacked_invalidations_.SetHandlerIsRegistered();

  Invalidation inv1 = Invalidation::Init(kObjectId_, 10, "payload");
  Invalidation inv2 = Invalidation::InitUnknownVersion(kObjectId_);
  AckHandle inv1_handle = inv1.ack_handle();

  unacked_invalidations_.Add(inv1);
  unacked_invalidations_.Add(inv2);

  unacked_invalidations_.Acknowledge(inv1_handle);

  SingleObjectInvalidationSet set = GetStoredInvalidations();
  EXPECT_EQ(1U, set.GetSize());
  EXPECT_TRUE(set.StartsWithUnknownVersion());
}

// Test drops.
TEST_F(UnackedInvalidationSetTest, Drop) {
  // inv2 is included in this test just to make sure invalidations that
  // are supposed to be unaffected by this operation will be unaffected.

  // We don't expect to be receiving acks or drops unless this flag is set.
  // Not that it makes much of a difference in behavior.
  unacked_invalidations_.SetHandlerIsRegistered();

  Invalidation inv1 = Invalidation::Init(kObjectId_, 10, "payload");
  Invalidation inv2 = Invalidation::Init(kObjectId_, 15, "payload");
  AckHandle inv1_handle = inv1.ack_handle();

  unacked_invalidations_.Add(inv1);
  unacked_invalidations_.Add(inv2);

  unacked_invalidations_.Drop(inv1_handle);

  SingleObjectInvalidationSet set = GetStoredInvalidations();
  ASSERT_EQ(2U, set.GetSize());
  EXPECT_TRUE(set.StartsWithUnknownVersion());
  EXPECT_EQ(15, set.rbegin()->version());
}

class UnackedInvalidationSetSerializationTest
    : public UnackedInvalidationSetTest {
 public:
  UnackedInvalidationSet SerializeDeserialize() {
    scoped_ptr<base::DictionaryValue> value = unacked_invalidations_.ToValue();
    UnackedInvalidationSet deserialized(kObjectId_);
    deserialized.ResetFromValue(*value.get());
    return deserialized;
  }
};

TEST_F(UnackedInvalidationSetSerializationTest, Empty) {
  UnackedInvalidationSet deserialized = SerializeDeserialize();
  EXPECT_THAT(unacked_invalidations_, Eq(deserialized));
}

TEST_F(UnackedInvalidationSetSerializationTest, OneInvalidation) {
  Invalidation inv = Invalidation::Init(kObjectId_, 10, "payload");
  unacked_invalidations_.Add(inv);

  UnackedInvalidationSet deserialized = SerializeDeserialize();
  EXPECT_THAT(unacked_invalidations_, Eq(deserialized));
}

TEST_F(UnackedInvalidationSetSerializationTest, WithUnknownVersion) {
  Invalidation inv1 = Invalidation::Init(kObjectId_, 10, "payload");
  Invalidation inv2 = Invalidation::InitUnknownVersion(kObjectId_);
  Invalidation inv3 = Invalidation::InitUnknownVersion(kObjectId_);
  unacked_invalidations_.Add(inv1);
  unacked_invalidations_.Add(inv2);
  unacked_invalidations_.Add(inv3);

  UnackedInvalidationSet deserialized = SerializeDeserialize();
  EXPECT_THAT(unacked_invalidations_, Eq(deserialized));
}

}  // namespace

}  // namespace syncer
