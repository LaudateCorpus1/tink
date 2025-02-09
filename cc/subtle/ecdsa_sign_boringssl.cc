// Copyright 2017 Google Inc.
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

#include "tink/subtle/ecdsa_sign_boringssl.h"

#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"
#include "tink/internal/bn_util.h"
#include "tink/internal/ec_util.h"
#include "tink/internal/err_util.h"
#include "tink/internal/md_util.h"
#include "tink/internal/ssl_unique_ptr.h"
#include "tink/internal/util.h"
#include "tink/subtle/common_enums.h"
#include "tink/subtle/subtle_util_boringssl.h"
#include "tink/util/errors.h"
#include "tink/util/statusor.h"

namespace crypto {
namespace tink {
namespace subtle {

namespace {

// Transforms ECDSA DER signature encoding to IEEE_P1363 encoding.
//
// The IEEE_P1363 signature's format is r || s, where r and s are zero-padded
// and have the same size in bytes as the order of the curve. For example, for
// NIST P-256 curve, r and s are zero-padded to 32 bytes.
//
// The DER signature is encoded using ASN.1
// (https://tools.ietf.org/html/rfc5480#appendix-A): ECDSA-Sig-Value :: =
// SEQUENCE { r INTEGER, s INTEGER }. In particular, the encoding is: 0x30 ||
// totalLength || 0x02 || r's length || r || 0x02 || s's length || s.
crypto::tink::util::StatusOr<std::string> DerToIeee(absl::string_view der,
                                                    const EC_KEY* key) {
  size_t field_size_in_bytes =
      (EC_GROUP_get_degree(EC_KEY_get0_group(key)) + 7) / 8;

  ECDSA_SIG* ecdsa_ptr = nullptr;
  const uint8_t* der_ptr = reinterpret_cast<const uint8_t*>(der.data());
  // Note: d2i_ECDSA_SIG is deprecated in BoringSSL, but it isn't in OpenSSL.
  internal::SslUniquePtr<ECDSA_SIG> ecdsa(
      d2i_ECDSA_SIG(&ecdsa_ptr, &der_ptr, der.size()));
  if (ecdsa == nullptr) {
    return util::Status(absl::StatusCode::kInternal, "d2i_ECDSA_SIG failed");
  }

  const BIGNUM* r_bn;
  const BIGNUM* s_bn;
  ECDSA_SIG_get0(ecdsa.get(), &r_bn, &s_bn);
  util::StatusOr<std::string> r =
      internal::BignumToString(r_bn, field_size_in_bytes);
  if (!r.ok()) {
    return r.status();
  }
  util::StatusOr<std::string> s =
      internal::BignumToString(s_bn, field_size_in_bytes);
  if (!s.ok()) {
    return s.status();
  }
  return absl::StrCat(*r, *s);
}

}  // namespace

// static
util::StatusOr<std::unique_ptr<EcdsaSignBoringSsl>> EcdsaSignBoringSsl::New(
    const SubtleUtilBoringSSL::EcKey& ec_key, HashType hash_type,
    EcdsaSignatureEncoding encoding) {
  auto status = internal::CheckFipsCompatibility<EcdsaSignBoringSsl>();
  if (!status.ok()) return status;

  // Check if the hash type is safe to use.
  util::Status is_safe = internal::IsHashTypeSafeForSignature(hash_type);
  if (!is_safe.ok()) {
    return is_safe;
  }
  util::StatusOr<const EVP_MD*> hash = internal::EvpHashFromHashType(hash_type);
  if (!hash.ok()) {
    return hash.status();
  }

  // Check curve.
  util::StatusOr<internal::SslUniquePtr<EC_GROUP>> group =
      internal::EcGroupFromCurveType(ec_key.curve);
  if (!group.ok()) {
    return group.status();
  }
  internal::SslUniquePtr<EC_KEY> key(EC_KEY_new());
  EC_KEY_set_group(key.get(), group->get());

  // Check key.
  util::StatusOr<internal::SslUniquePtr<EC_POINT>> pub_key =
      internal::GetEcPoint(ec_key.curve, ec_key.pub_x, ec_key.pub_y);
  if (!pub_key.ok()) {
    return pub_key.status();
  }

  if (!EC_KEY_set_public_key(key.get(), pub_key->get())) {
    return util::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("Invalid public key: ", internal::GetSslErrors()));
  }

  internal::SslUniquePtr<BIGNUM> priv_key(
      BN_bin2bn(ec_key.priv.data(), ec_key.priv.size(), nullptr));
  if (!EC_KEY_set_private_key(key.get(), priv_key.get())) {
    return util::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("Invalid private key: ", internal::GetSslErrors()));
  }

  // Sign.
  std::unique_ptr<EcdsaSignBoringSsl> sign(
      new EcdsaSignBoringSsl(std::move(key), *hash, encoding));
  return std::move(sign);
}

EcdsaSignBoringSsl::EcdsaSignBoringSsl(internal::SslUniquePtr<EC_KEY> key,
                                       const EVP_MD* hash,
                                       EcdsaSignatureEncoding encoding)
    : key_(std::move(key)), hash_(hash), encoding_(encoding) {}

util::StatusOr<std::string> EcdsaSignBoringSsl::Sign(
    absl::string_view data) const {
  // BoringSSL expects a non-null pointer for data,
  // regardless of whether the size is 0.
  data = internal::EnsureStringNonNull(data);

  // Compute the digest.
  unsigned int digest_size;
  uint8_t digest[EVP_MAX_MD_SIZE];
  if (1 != EVP_Digest(data.data(), data.size(), digest, &digest_size, hash_,
                      nullptr)) {
    return util::Status(absl::StatusCode::kInternal,
                        "Could not compute digest.");
  }

  // Compute the signature.
  std::vector<uint8_t> buffer(ECDSA_size(key_.get()));
  unsigned int sig_length;
  if (1 != ECDSA_sign(0 /* unused */, digest, digest_size, buffer.data(),
                      &sig_length, key_.get())) {
    return util::Status(absl::StatusCode::kInternal, "Signing failed.");
  }

  if (encoding_ == subtle::EcdsaSignatureEncoding::IEEE_P1363) {
    auto status_or_sig = DerToIeee(
        std::string(reinterpret_cast<char*>(buffer.data()), sig_length),
        key_.get());
    if (!status_or_sig.ok()) {
      return status_or_sig.status();
    }
    return status_or_sig.ValueOrDie();
  }

  return std::string(reinterpret_cast<char*>(buffer.data()), sig_length);
}

}  // namespace subtle
}  // namespace tink
}  // namespace crypto
