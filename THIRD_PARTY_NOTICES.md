# Third-party notices

## This integration

The original Linux Resolve integration and documentation are copyright (c) 2026
thewrz and contributors, and are made available under the Corridor Key Licence
version 2.0 in the root `LICENSE` file. That file reproduces the upstream terms
unchanged, including their attribution, free-access and share-alike conditions.
Third-party files retain their separate copyright notices and licenses below.

## CorridorKey 2.0

CorridorKey is copyright Corridor Digital LLC. This independent local integration
uses the original [CorridorKey 2.0](https://github.com/nikopueringer/CorridorKey2.0)
runtime at commit `485f5e9ab759ead6138cc990776184b59d6ea971`, with main weights from
`nikopueringer/CorridorKey2.0` at revision
`cf44a8db3f024bb49fd59daa408848ccf05f7477`.

The separately downloaded code and weights retain their **Corridor Key Licence
version 2.0**, comprising CC BY-NC-SA 4.0 and additional terms. The authoritative
text is in `.deps/CorridorKey2.0/LICENSE` and the upstream repository at that commit.
The adapter does not grant rights to repackage or distribute those dependencies.
The installed OFX bundle contains neither the upstream implementation nor weights.
The setup script retrieves the runtime and weights separately from their publishers.

The upstream foundation dependencies retain their own licenses, documented in
`.deps/CorridorKey2.0/THIRD_PARTY_NOTICES.md`:

| Component | Publisher | License |
| --- | --- | --- |
| C-RADIOv4-SO400M | NVIDIA | NVIDIA Open Model License |
| MoGe-2 ViT-B | Microsoft / Ruicheng | MIT |
| DINOv2 included with MoGe | Meta | Apache-2.0 |
| RVM small | Google DeepMind | Apache-2.0 code, CC BY 4.0 weights |

The NVIDIA and MoGe model revisions are pinned by the upstream downloader;
RVM is checked against its published MD5 by that downloader.

## Native adapter dependencies

- **OpenSSL libcrypto**: OpenSSL Project and contributors, Apache-2.0.
  Dynamically links the system library for SHA-256 frame verification; the bundle
  does not redistribute OpenSSL.
- **OpenFX Support Library**: The Open Effects Association Ltd, copyright
  2004–2007; author Bruno Nicoletti. Compiled from the SDK supplied with Resolve.
  The BSD-style notice is reproduced in `licenses/OpenFX-Support.txt` and in the
  installed bundle's `Contents/Resources/licenses/` directory.
- **JSON for Modern C++ 3.12.0**: Niels Lohmann and contributors, MIT.
  Vendored from commit `65ee68451d8eb2b5f3a30b410476ab83deb3289b`.
  See `vendor/nlohmann/LICENSE.MIT` and `licenses/nlohmann-json-MIT.txt`.
- **GNU C++ and GCC runtimes**: Free Software Foundation and contributors,
  GPLv3 with GCC Runtime Library Exception 3.1. Linked privately to prevent
  incompatible host symbols from overriding plugin internals. See
  `licenses/GPL-3.0.txt` and `licenses/GCC-Runtime-Exception.txt`.

Python dependencies and their exact versions/hashes are recorded in `uv.lock`.
Their license metadata remains in the separately installed `.venv` environment.
No third-party branding or affiliation is implied by this integration.
