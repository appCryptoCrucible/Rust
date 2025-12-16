#include "crypto/secp256k1.hpp"
#ifdef HAVE_SECP256K1
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#endif
#include <stdexcept>

namespace Crypto {
#ifdef HAVE_SECP256K1
  static secp256k1_context* GetCtx() {
    static secp256k1_context* ctx = []{
      return secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }();
    return ctx;
  }
#endif

  Signature SignDigest(const std::vector<unsigned char>& priv32, const std::vector<unsigned char>& digest32) {
#ifdef HAVE_SECP256K1
    if (priv32.size() != 32 || digest32.size() != 32) throw std::invalid_argument("bad key/digest size");
    secp256k1_ecdsa_recoverable_signature sig_raw;
    if (!secp256k1_ecdsa_sign_recoverable(GetCtx(), &sig_raw, digest32.data(), priv32.data(), nullptr, nullptr))
      throw std::runtime_error("sign failed");
    unsigned char out64[64]; int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(GetCtx(), out64, &recid, &sig_raw);
    Signature sig; sig.r.assign(out64, out64 + 32); sig.s.assign(out64 + 32, out64 + 64); sig.v = static_cast<unsigned char>(27 + recid);
    return sig;
#else
    throw std::runtime_error("secp256k1 not available");
#endif
  }

  std::vector<unsigned char> PublicKeyFromPrivate(const std::vector<unsigned char>& priv32) {
#ifdef HAVE_SECP256K1
    if (priv32.size() != 32) throw std::invalid_argument("bad key size");
    secp256k1_pubkey pub;
    if (!secp256k1_ec_pubkey_create(GetCtx(), &pub, priv32.data()))
      throw std::runtime_error("pubkey create failed");
    unsigned char out[65]; size_t outlen = sizeof(out);
    secp256k1_ec_pubkey_serialize(GetCtx(), out, &outlen, &pub, SECP256K1_EC_UNCOMPRESSED);
    return std::vector<unsigned char>(out, out + outlen);
#else
    throw std::runtime_error("secp256k1 not available");
#endif
  }
}

