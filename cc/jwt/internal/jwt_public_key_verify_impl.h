// Copyright 2021 Google LLC
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

#ifndef TINK_JWT_INTERNAL_JWT_PUBLIC_KEY_VERIFY_IMPL_H_
#define TINK_JWT_INTERNAL_JWT_PUBLIC_KEY_VERIFY_IMPL_H_

#include "absl/strings/string_view.h"
#include "tink/jwt/internal/jwt_public_key_verify_internal.h"
#include "tink/jwt/jwt_validator.h"
#include "tink/jwt/raw_jwt.h"
#include "tink/jwt/verified_jwt.h"
#include "tink/public_key_verify.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"

namespace crypto {
namespace tink {
namespace jwt_internal {

class JwtPublicKeyVerifyImpl : public JwtPublicKeyVerifyInternal {
 public:
  explicit JwtPublicKeyVerifyImpl(
      std::unique_ptr<crypto::tink::PublicKeyVerify> verify,
      absl::string_view algorithm,
      absl::optional<absl::string_view> custom_kid) {
    verify_ = std::move(verify);
    algorithm_ = std::string(algorithm);
    if (custom_kid.has_value()) {
      custom_kid_ = std::string(*custom_kid);
    }
  }

  crypto::tink::util::StatusOr<VerifiedJwt> VerifyAndDecodeWithKid(
      absl::string_view compact, const JwtValidator& validator,
      absl::optional<absl::string_view> kid) const override;

 private:
  std::unique_ptr<crypto::tink::PublicKeyVerify> verify_;
  std::string algorithm_;
  // custom_kid may be set when a key is converted from another format, for
  // example JWK. It does not have any relation to the key id. It can only be
  // set for keys with output prefix RAW.
  absl::optional<std::string> custom_kid_;
};

}  // namespace jwt_internal
}  // namespace tink
}  // namespace crypto

#endif  // TINK_JWT_INTERNAL_JWT_PUBLIC_KEY_VERIFY_IMPL_H_
