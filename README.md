# libechidna

Layout from https://cliutils.gitlab.io/modern-cmake/chapters/basics/structure.html

```bash
apt-get install libnuma-dev libyaml-dev libssl-dev
export CC=/usr/bin/clang-15
export CXX=/usr/bin/clang++-15

cmake -GNinja -S . -B build
cmake --build build
cmake --build build --target test
```



OpenSSL


# generate private key
# generate a certificate signing request (CSR):
# Generate a self-signed certificate

openssl genpkey -algorithm RSA -out private.key -pkeyopt rsa_keygen_bits:2048
openssl req -new -key private.key -out csr.csr
openssl x509 -req -in csr.csr -signkey private.key -out certificate.crt -days 365

# verify
openssl x509 -in certificate.crt -text -noout


openssl s_server -cert certificate.crt -key private.key -accept 4433 -www
openssl s_client -connect localhost:9988 -CAfile certificate.crt  -servername localhost