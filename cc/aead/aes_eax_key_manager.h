// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef TINK_AEAD_AES_EAX_KEY_MANAGER_H_
#define TINK_AEAD_AES_EAX_KEY_MANAGER_H_

#include <string>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "tink/aead.h"
#include "tink/core/key_type_manager.h"
#include "tink/subtle/aes_eax_boringssl.h"
#include "tink/subtle/random.h"
#include "tink/util/constants.h"
#include "tink/util/errors.h"
#include "tink/util/protobuf_helper.h"
#include "tink/util/secret_data.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"
#include "tink/util/validation.h"
#include "proto/aes_eax.pb.h"

namespace crypto {
namespace tink {

class AesEaxKeyManager
    : public KeyTypeManager<google::crypto::tink::AesEaxKey,
                            google::crypto::tink::AesEaxKeyFormat, List<Aead>> {
 public:
  class AeadFactory : public PrimitiveFactory<Aead> {
    crypto::tink::util::StatusOr<std::unique_ptr<Aead>> Create(
        const google::crypto::tink::AesEaxKey& key) const override {
      return subtle::AesEaxBoringSsl::New(
          util::SecretDataFromStringView(key.key_value()),
          key.params().iv_size());
    }
  };

  AesEaxKeyManager() : KeyTypeManager(absl::make_unique<AeadFactory>()) {}

  uint32_t get_version() const override { return 0; }

  google::crypto::tink::KeyData::KeyMaterialType key_material_type()
      const override {
    return google::crypto::tink::KeyData::SYMMETRIC;
  }

  const std::string& get_key_type() const override { return key_type_; }

  crypto::tink::util::Status ValidateKey(
      const google::crypto::tink::AesEaxKey& key) const override {
    crypto::tink::util::Status status =
        ValidateVersion(key.version(), get_version());
    if (!status.ok()) return status;
    status = ValidateKeySize(key.key_value().size());
    if (!status.ok()) return status;
    return ValidateIvSize(key.params().iv_size());
  }

  crypto::tink::util::Status ValidateKeyFormat(
      const google::crypto::tink::AesEaxKeyFormat& key_format) const override {
    crypto::tink::util::Status status = ValidateKeySize(key_format.key_size());
    if (!status.ok()) return status;
    return ValidateIvSize(key_format.params().iv_size());
  }

  crypto::tink::util::StatusOr<google::crypto::tink::AesEaxKey> CreateKey(
      const google::crypto::tink::AesEaxKeyFormat& key_format) const override {
    google::crypto::tink::AesEaxKey aes_eax_key;
    aes_eax_key.set_version(get_version());
    aes_eax_key.set_key_value(
        subtle::Random::GetRandomBytes(key_format.key_size()));
    aes_eax_key.mutable_params()->set_iv_size(
        key_format.params().iv_size());
    return aes_eax_key;
  }

 private:
  crypto::tink::util::Status ValidateKeySize(uint32_t key_size) const {
    if (key_size != 16 && key_size != 32) {
      return crypto::tink::util::Status(
          absl::StatusCode::kInvalidArgument,
          absl::StrCat("Invalid key size: ", key_size,
                       " bytes, expected 16 or 32 bytes."));
    }
    return crypto::tink::util::OkStatus();
  }

  crypto::tink::util::Status ValidateIvSize(uint32_t iv_size) const {
    if (iv_size != 12 && iv_size != 16) {
      return crypto::tink::util::Status(
          absl::StatusCode::kInvalidArgument,
          absl::StrCat("Invalid IV size: ", iv_size,
                       " bytes, expected 12 or 16 bytes."));
    }
    return crypto::tink::util::OkStatus();
  }

  const std::string key_type_ = absl::StrCat(
      kTypeGoogleapisCom, google::crypto::tink::AesEaxKey().GetTypeName());
};

}  // namespace tink
}  // namespace crypto

#endif  // TINK_AEAD_AES_EAX_KEY_MANAGER_H_
