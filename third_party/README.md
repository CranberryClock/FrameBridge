# Third-party boundary

Governed NVIDIA DLSS/NGX binaries remain outside this repository. Keep notices and license references in the dependency ledger, but never commit SDK binaries, credentials, application identities, or local paths.

TCW-005 pins IXWebSocket `v12.0.1` at commit `64fae7676bd8fe31f7cb4bcde7a6841892dad65e` as a native RFC 6455 server dependency. It is licensed under the BSD 3-Clause license; see `third_party/ixwebsocket/LICENSE.txt`. CMake builds it with TLS and compression disabled for loopback-only use.

TCW-005 pins nlohmann/json `v3.11.3` as the strict native hello parser. It is licensed under the MIT license; see `third_party/nlohmann-json/LICENSE.MIT`. NVIDIA dependencies remain external and uncommitted.
