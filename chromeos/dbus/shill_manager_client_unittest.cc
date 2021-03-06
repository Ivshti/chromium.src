// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/values.h"
#include "chromeos/dbus/shill_client_unittest_base.h"
#include "chromeos/dbus/shill_manager_client.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/values_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

using testing::_;
using testing::ByRef;

namespace chromeos {

namespace {

void ExpectStringArguments(const std::vector<std::string>& arguments,
                           dbus::MessageReader* reader) {
  for (std::vector<std::string>::const_iterator iter = arguments.begin();
       iter != arguments.end(); ++iter) {
    std::string arg_string;
    ASSERT_TRUE(reader->PopString(&arg_string));
    EXPECT_EQ(*iter, arg_string);
  }
  EXPECT_FALSE(reader->HasMoreData());
}

void ExpectStringArgumentsFollowedByObjectPath(
      const std::vector<std::string>& arguments,
      const dbus::ObjectPath& object_path,
      dbus::MessageReader* reader) {
  for (std::vector<std::string>::const_iterator iter = arguments.begin();
       iter != arguments.end(); ++iter) {
    std::string arg_string;
    ASSERT_TRUE(reader->PopString(&arg_string));
    EXPECT_EQ(*iter, arg_string);
  }
  dbus::ObjectPath path;
  ASSERT_TRUE(reader->PopObjectPath(&path));
  EXPECT_EQ(object_path, path);
  EXPECT_FALSE(reader->HasMoreData());
}


}  // namespace

class ShillManagerClientTest : public ShillClientUnittestBase {
 public:
  ShillManagerClientTest()
      : ShillClientUnittestBase(shill::kFlimflamManagerInterface,
                                dbus::ObjectPath(shill::kFlimflamServicePath)) {
  }

  virtual void SetUp() {
    ShillClientUnittestBase::SetUp();
    // Create a client with the mock bus.
    client_.reset(ShillManagerClient::Create(REAL_DBUS_CLIENT_IMPLEMENTATION));
    client_->Init(mock_bus_.get());
    // Run the message loop to run the signal connection result callback.
    message_loop_.RunUntilIdle();
  }

  virtual void TearDown() {
    ShillClientUnittestBase::TearDown();
  }

 protected:
  scoped_ptr<ShillManagerClient> client_;
};

TEST_F(ShillManagerClientTest, PropertyChanged) {
  // Create a signal.
  base::FundamentalValue kOfflineMode(true);
  dbus::Signal signal(shill::kFlimflamManagerInterface,
                      shill::kMonitorPropertyChanged);
  dbus::MessageWriter writer(&signal);
  writer.AppendString(shill::kOfflineModeProperty);
  dbus::AppendBasicTypeValueData(&writer, kOfflineMode);

  // Set expectations.
  MockPropertyChangeObserver observer;
  EXPECT_CALL(observer,
              OnPropertyChanged(shill::kOfflineModeProperty,
                                ValueEq(ByRef(kOfflineMode)))).Times(1);

  // Add the observer
  client_->AddPropertyChangedObserver(&observer);

  // Run the signal callback.
  SendPropertyChangedSignal(&signal);

  // Remove the observer.
  client_->RemovePropertyChangedObserver(&observer);

  // Make sure it's not called anymore.
  EXPECT_CALL(observer, OnPropertyChanged(_, _)).Times(0);

  // Run the signal callback again and make sure the observer isn't called.
  SendPropertyChangedSignal(&signal);
}

TEST_F(ShillManagerClientTest, GetProperties) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  dbus::MessageWriter array_writer(NULL);
  writer.OpenArray("{sv}", &array_writer);
  dbus::MessageWriter entry_writer(NULL);
  array_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString(shill::kOfflineModeProperty);
  entry_writer.AppendVariantOfBool(true);
  array_writer.CloseContainer(&entry_writer);
  writer.CloseContainer(&array_writer);

  // Create the expected value.
  base::DictionaryValue value;
  value.SetWithoutPathExpansion(shill::kOfflineModeProperty,
                                base::Value::CreateBooleanValue(true));
  // Set expectations.
  PrepareForMethodCall(shill::kGetPropertiesFunction,
                       base::Bind(&ExpectNoArgument),
                       response.get());
  // Call method.
  client_->GetProperties(base::Bind(&ExpectDictionaryValueResult, &value));
  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, GetNetworksForGeolocation) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());

  dbus::MessageWriter writer(response.get());
  dbus::MessageWriter type_dict_writer(NULL);
  writer.OpenArray("{sv}", &type_dict_writer);
  dbus::MessageWriter type_entry_writer(NULL);
  type_dict_writer.OpenDictEntry(&type_entry_writer);
  type_entry_writer.AppendString(shill::kTypeWifi);
  dbus::MessageWriter variant_writer(NULL);
  type_entry_writer.OpenVariant("aa{ss}", &variant_writer);
  dbus::MessageWriter wap_list_writer(NULL);
  variant_writer.OpenArray("a{ss}", &wap_list_writer);
  dbus::MessageWriter property_dict_writer(NULL);
  wap_list_writer.OpenArray("{ss}", &property_dict_writer);
  dbus::MessageWriter property_entry_writer(NULL);
  property_dict_writer.OpenDictEntry(&property_entry_writer);
  property_entry_writer.AppendString(shill::kGeoMacAddressProperty);
  property_entry_writer.AppendString("01:23:45:67:89:AB");
  property_dict_writer.CloseContainer(&property_entry_writer);
  wap_list_writer.CloseContainer(&property_dict_writer);
  variant_writer.CloseContainer(&wap_list_writer);
  type_entry_writer.CloseContainer(&wap_list_writer);
  type_dict_writer.CloseContainer(&type_entry_writer);
  writer.CloseContainer(&type_dict_writer);


  // Create the expected value.
  base::DictionaryValue type_dict_value;
  base::ListValue* type_entry_value = new base::ListValue;
  base::DictionaryValue* property_dict_value = new base::DictionaryValue;
  property_dict_value->SetWithoutPathExpansion(
      shill::kGeoMacAddressProperty,
      base::Value::CreateStringValue("01:23:45:67:89:AB"));
  type_entry_value->Append(property_dict_value);
  type_dict_value.SetWithoutPathExpansion("wifi", type_entry_value);

  // Set expectations.
  PrepareForMethodCall(shill::kGetNetworksForGeolocation,
                       base::Bind(&ExpectNoArgument),
                       response.get());
  // Call method.
  client_->GetNetworksForGeolocation(base::Bind(&ExpectDictionaryValueResult,
                                                &type_dict_value));

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, SetProperty) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  // Set expectations.
  base::StringValue value("portal list");
  PrepareForMethodCall(shill::kSetPropertyFunction,
                       base::Bind(ExpectStringAndValueArguments,
                                  shill::kCheckPortalListProperty,
                                  &value),
                       response.get());
  // Call method.
  MockClosure mock_closure;
  MockErrorCallback mock_error_callback;
  client_->SetProperty(shill::kCheckPortalListProperty,
                       value,
                       mock_closure.GetCallback(),
                       mock_error_callback.GetCallback());
  EXPECT_CALL(mock_closure, Run()).Times(1);
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, RequestScan) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  // Set expectations.
  PrepareForMethodCall(shill::kRequestScanFunction,
                       base::Bind(&ExpectStringArgument, shill::kTypeWifi),
                       response.get());
  // Call method.
  MockClosure mock_closure;
  MockErrorCallback mock_error_callback;
  client_->RequestScan(shill::kTypeWifi,
                       mock_closure.GetCallback(),
                       mock_error_callback.GetCallback());
  EXPECT_CALL(mock_closure, Run()).Times(1);
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, EnableTechnology) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  // Set expectations.
  PrepareForMethodCall(shill::kEnableTechnologyFunction,
                       base::Bind(&ExpectStringArgument, shill::kTypeWifi),
                       response.get());
  // Call method.
  MockClosure mock_closure;
  MockErrorCallback mock_error_callback;
  client_->EnableTechnology(shill::kTypeWifi,
                            mock_closure.GetCallback(),
                            mock_error_callback.GetCallback());
  EXPECT_CALL(mock_closure, Run()).Times(1);
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, DisableTechnology) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  // Set expectations.
  PrepareForMethodCall(shill::kDisableTechnologyFunction,
                       base::Bind(&ExpectStringArgument, shill::kTypeWifi),
                       response.get());
  // Call method.
  MockClosure mock_closure;
  MockErrorCallback mock_error_callback;
  client_->DisableTechnology(shill::kTypeWifi,
                             mock_closure.GetCallback(),
                             mock_error_callback.GetCallback());
  EXPECT_CALL(mock_closure, Run()).Times(1);
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, ConfigureService) {
  // Create response.
  const dbus::ObjectPath object_path("/");
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  writer.AppendObjectPath(object_path);
  // Create the argument dictionary.
  scoped_ptr<base::DictionaryValue> arg(CreateExampleServiceProperties());
  // Set expectations.
  PrepareForMethodCall(shill::kConfigureServiceFunction,
                       base::Bind(&ExpectDictionaryValueArgument, arg.get()),
                       response.get());
  // Call method.
  MockErrorCallback mock_error_callback;
  client_->ConfigureService(*arg,
                            base::Bind(&ExpectObjectPathResultWithoutStatus,
                                       object_path),
                            mock_error_callback.GetCallback());
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, GetService) {
  // Create response.
  const dbus::ObjectPath object_path("/");
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  writer.AppendObjectPath(object_path);
  // Create the argument dictionary.
  scoped_ptr<base::DictionaryValue> arg(CreateExampleServiceProperties());
  // Set expectations.
  PrepareForMethodCall(shill::kGetServiceFunction,
                       base::Bind(&ExpectDictionaryValueArgument, arg.get()),
                       response.get());
  // Call method.
  MockErrorCallback mock_error_callback;
  client_->GetService(*arg,
                      base::Bind(&ExpectObjectPathResultWithoutStatus,
                                 object_path),
                      mock_error_callback.GetCallback());
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, VerifyDestination) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  bool expected = true;
  writer.AppendBool(expected);
  // Set expectations.
  std::vector<std::string> arguments;
  arguments.push_back("certificate");
  arguments.push_back("public_key");
  arguments.push_back("nonce");
  arguments.push_back("signed_data");
  arguments.push_back("device_serial");
  arguments.push_back("device_ssid");
  arguments.push_back("device_bssid");
  PrepareForMethodCall(shill::kVerifyDestinationFunction,
                       base::Bind(&ExpectStringArguments, arguments),
                       response.get());

  // Call method.
  MockErrorCallback mock_error_callback;
  ShillManagerClient::VerificationProperties properties;
  properties.certificate = arguments[0];
  properties.public_key = arguments[1];
  properties.nonce = arguments[2];
  properties.signed_data = arguments[3];
  properties.device_serial = arguments[4];
  properties.device_ssid = arguments[5];
  properties.device_bssid = arguments[6];
  client_->VerifyDestination(
      properties,
      base::Bind(&ExpectBoolResultWithoutStatus, expected),
      mock_error_callback.GetCallback());
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, VerifyAndEncryptCredentials) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  std::string expected = "encrypted_credentials";
  writer.AppendString(expected);
  // Set expectations.
  std::vector<std::string> arguments;
  arguments.push_back("certificate");
  arguments.push_back("public_key");
  arguments.push_back("nonce");
  arguments.push_back("signed_data");
  arguments.push_back("device_serial");
  arguments.push_back("device_ssid");
  arguments.push_back("device_bssid");
  std::string service_path = "/";
  dbus::ObjectPath service_path_obj(service_path);
  PrepareForMethodCall(shill::kVerifyAndEncryptCredentialsFunction,
                       base::Bind(&ExpectStringArgumentsFollowedByObjectPath,
                                  arguments,
                                  service_path_obj),
                       response.get());

  // Call method.
  MockErrorCallback mock_error_callback;
  ShillManagerClient::VerificationProperties properties;
  properties.certificate = arguments[0];
  properties.public_key = arguments[1];
  properties.nonce = arguments[2];
  properties.signed_data = arguments[3];
  properties.device_serial = arguments[4];
  properties.device_ssid = arguments[5];
  properties.device_bssid = arguments[6];
  client_->VerifyAndEncryptCredentials(
      properties,
      service_path,
      base::Bind(&ExpectStringResultWithoutStatus, expected),
      mock_error_callback.GetCallback());
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

TEST_F(ShillManagerClientTest, VerifyAndEncryptData) {
  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  std::string expected = "encrypted_data";
  writer.AppendString(expected);
  // Set expectations.
  std::vector<std::string> arguments;
  arguments.push_back("certificate");
  arguments.push_back("public_key");
  arguments.push_back("nonce");
  arguments.push_back("signed_data");
  arguments.push_back("device_serial");
  arguments.push_back("device_ssid");
  arguments.push_back("device_bssid");
  arguments.push_back("data");
  PrepareForMethodCall(shill::kVerifyAndEncryptDataFunction,
                       base::Bind(&ExpectStringArguments, arguments),
                       response.get());

  // Call method.
  MockErrorCallback mock_error_callback;
  ShillManagerClient::VerificationProperties properties;
  properties.certificate = arguments[0];
  properties.public_key = arguments[1];
  properties.nonce = arguments[2];
  properties.signed_data = arguments[3];
  properties.device_serial = arguments[4];
  properties.device_ssid = arguments[5];
  properties.device_bssid = arguments[6];
  client_->VerifyAndEncryptData(
      properties,
      arguments[7],
      base::Bind(&ExpectStringResultWithoutStatus, expected),
      mock_error_callback.GetCallback());
  EXPECT_CALL(mock_error_callback, Run(_, _)).Times(0);

  // Run the message loop.
  message_loop_.RunUntilIdle();
}

}  // namespace chromeos
