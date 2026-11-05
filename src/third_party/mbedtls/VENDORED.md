# mbedTLS, vendored

Priiloader's updater fetches GitHub releases over HTTPS. This is the TLS library under it
(`src/priiloader/tls` is our layer on top).

| | |
|---|---|
| Version | mbedTLS 3.6.7 (tag `mbedtls-3.6.7`, 2026-07-07, the latest 3.6 LTS release) |
| Tag object | `627361b09e6f6e3eac756297a370a591cf310ab9` |
| Source | https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 |
| SHA-256 of the tarball | `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` (matches the release's `sha256sum.txt` and GitHub's digest) |
| Vendored on | 2026-11-04 |
| Licence | Apache-2.0 OR GPL-2.0-or-later (`LICENSE`); we use it under Apache-2.0 |

## What is copied

- `library/`: the 43 `.c` files our config needs (list: `ls library/*.c`) and every `.h` of the upstream `library/`.
- `include/mbedtls/`, `include/psa/`: all upstream headers, unchanged.
- `LICENSE`.
- Not copied: tests, programs, docs, `3rdparty/`, `tf-psa-crypto`, the CMake build, TLS 1.3, DTLS, server code, PSA crypto sources.

Our own files, not upstream: `priiloader_mbedtls_config.h` (the configuration), `Makefile` (builds `build/libmbedtls.a`
with devkitPPC, no CMake), this file. No upstream file is edited.

## Configuration

`priiloader_mbedtls_config.h`, selected with `-DMBEDTLS_CONFIG_FILE='"priiloader_mbedtls_config.h"'`: TLS 1.2 client
only; ECDHE-ECDSA and ECDHE-RSA with AES-GCM or ChaCha20-Poly1305; SHA-256 and SHA-384 (certificates from the roots below
are signed with both); RSA (PKCS#1 v1.5 and PSS), ECDSA on secp256r1, secp384r1 and x25519; X.509 parse and verify. No
PSA, no TLS 1.3, no platform entropy (`tls_entropy.c` supplies it), no server. Receive buffer 16384 bytes (GitHub sends
full records), send buffer 2048. A missing `MBEDTLS_SHA384_C` makes every SHA-384-signed root fail to parse with
`-0x262e` (unsupported signature algorithm OID), so keep it.

## Root certificates (`src/priiloader/tls/source/tls_roots.c`, generated)

Only the roots the update hosts chain to, from Mozilla's bundle (https://curl.se/ca/cacert.pem, SHA-256
`796f1389...3cbe` verified when generated):

| Root | Expires | Why |
|---|---|---|
| USERTrust ECC Certification Authority | 2038-01-18 | cross-signs Sectigo E46, which signs `api.github.com` and `github.com` |
| USERTrust RSA Certification Authority | 2038-01-18 | same family, RSA path |
| Sectigo Public Server Authentication Root E46 | 2046-03-21 | GitHub's own chain (`Sectigo E36`) ends here |
| Sectigo Public Server Authentication Root R46 | 2046-03-21 | RSA sibling of E46 |
| ISRG Root X1 | 2035-06-04 | Let's Encrypt, which signs `objects.githubusercontent.com` and `release-assets.githubusercontent.com` |
| ISRG Root X2 | 2040-09-17 | Let's Encrypt's ECDSA root |

Chains seen on 2026-11-04 (openssl s_client): `api.github.com` and `github.com` -> Sectigo E36 -> Sectigo E46 (cross-signed
by USERTrust ECC); `objects.githubusercontent.com` and `release-assets.githubusercontent.com` -> Let's Encrypt YR1 ->
Root YR -> ISRG Root X1. Captured copies are in `tests/host/tls/chains/` for the host test.

**No DigiCert roots.** GitHub used DigiCert until 2025; its certificates now come from Sectigo and Let's Encrypt. If
GitHub moves again the updater fails closed (`TLS_ERR_CERT_UNTRUSTED`): add the new root and ship a release. Every root
here outlives 2035; check `ISRG Root X1` first (earliest) at each release.

The Wii's clock decides certificate validity. `tls_open` reports `TLS_ERR_CLOCK` when the dates are the only failure and
`time()` is outside 2026-01-01..2050-01-01 (`TLS_CLOCK_MIN`, `TLS_CLOCK_MAX` in `tls.h`).

## Regenerate the roots

```sh
curl -O https://curl.se/ca/cacert.pem
curl -O https://curl.se/ca/cacert.pem.sha256     # check it
python tools/gen_tls_roots.py cacert.pem
python -m unittest tests.test_host_tls
```

Edit `ROOTS` in `tools/gen_tls_roots.py` to change the list, and review the diff of `tls_roots.c`: it is the trust anchor
list of the updater.

## Update mbedTLS

Download the new tarball, verify its SHA-256 against `sha256sum.txt` from the release, replace `library/*.c`,
`library/*.h` and `include/`, keep our three files, rebuild (`make -C src/third_party/mbedtls`), run
`make warnings` there and the host and Dolphin TLS tests, and update the table above. A new `.c` file may be needed when
upstream moves code between files (`pk_ecc.c` was one); a missing one shows as an undefined symbol at link time.

## Warnings

The vendored files compile with `-w` in the normal build. `make -C src/third_party/mbedtls warnings` compiles them again
with `-Wall -Wextra -Wwrite-strings -Wmissing-declarations -Wformat=2 -Wno-unused-parameter` (`-fsyntax-only`); with
devkitPPC r51 (gcc 16.2) and our config it prints nothing.
