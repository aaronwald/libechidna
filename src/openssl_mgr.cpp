#include "echidna/openssl_mgr.hpp"

int coypu::net::ssl::VerifyCTX(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
  return preverify_ok;
}
