# MD4C provenance

This directory vendors the parser-only source files from
[mity/md4c](https://github.com/mity/md4c) `release-0.5.2`, commit
`729e6b8b320caa96328968ab27d7db2235e4fb47`.

MD4C is licensed under the MIT License. `LICENSE.md` is copied unchanged from
that upstream revision. Files retained here are only `md4c.c`, `md4c.h`,
`entity.c`, and `entity.h`. The two headers add macros that rename the exported
`md_parse` and `entity_lookup` symbols to `stmd_md_parse` and
`stmd_entity_lookup`, preventing collisions with another vendored MD4C copy.
