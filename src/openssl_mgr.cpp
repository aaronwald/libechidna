#include "echidna/openssl_mgr.hpp"

int coypu::net::ssl::VerifyCTX(int preverify_ok [[maybe_unused]], X509_STORE_CTX *x509_ctx [[maybe_unused]])
{
  // return preverify_ok;
  return 1;
}
