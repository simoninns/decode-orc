# Migration from legacy vhs-decode builds

## Legacy JSON metadata

The current version of Decode-Orc (and the ld-decode-tools Decode-Orc replaces) require TBC files with metadata in the current SQLite format (previously ld-decode used JSON rather than SQLite).  Although older JSON metadata decodes will be accepted after a warning, they lack some of the newer fields used by Decode-Orc and may cause the tools to emit warnings about missing parameters.

If you have old decodes the recommended course of action is to re-decode the capture with a current version of ld-decode or vhs-decode.  Do not rely on post-decoded files as preservation artifacts as the decoder functionality will vary over time and the post-decode artifact is not deterministic (TL;DR - re-decoding is preservation - keeping old TBC files is not).

## Separate Y/C sources

Legacy versions of vhs-decode produced TBC Y/C pairs with the format `<name>_chroma.tbc` and `<name>.tbc`.

In order to use these files with Decode-Orc you will need to simply rename them.  `<name>_chroma.tbc` should become `<name>.tbcc` and `<name>.tbc` should become `<name>.tbcy`.  This is used by Decode-Orc to distinguish composite and separate sources when using features such as "quick project".

