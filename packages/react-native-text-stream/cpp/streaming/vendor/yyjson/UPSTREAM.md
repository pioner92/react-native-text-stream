# yyjson provenance

Vendored from yyjson **0.10.0**, Git tag commit `a963bbe338fb0a1e862637d1fc1389e357a07629`.

- Upstream: https://github.com/ibireme/yyjson
- License: MIT (`LICENSE.md`)
- Source SHA-256 before local symbol prefixing:
  - `yyjson.c`: `98489c068ab65298320710e5be4c1090af1b5676e41301a3525b2e732641aace`
  - `yyjson.h`: `adce02aef5dfd868f7c0b632bd23bc0e79fa710b70d93bfe334c2bd15b21a41d`
- Local patch: all public and internal `yyjson_` identifiers are renamed to
  `styyjson_` to avoid interposition with other vendored libraries.
