# Third-party components

Original Pinloom application code is Apache-2.0; see LICENSE and NOTICE.
This does not relicense the components or assets below.

| Component | License / source | Retained material |
| --- | --- | --- |
| ElaWidgetTools | MIT; Liniyous, upstream `454cac2d57a47d3cc28577dc817793aec1881ca7` | `thirdparty/elawidgettools/LICENSE`, `UPSTREAM-REVISION.md`, patches |
| ZeroSlack compatibility changes | Apache-2.0; RutenunexJX | `thirdparty/elawidgettools/ZeroSlack-Apache-2.0.txt`; exact imports recorded in `UPSTREAM-REVISION.md` |
| Font Awesome Free Solid 6.7.2 | SIL OFL 1.1; Fonticons, Inc. | `thirdparty/elawidgettools/Font/FontAwesome-LICENSE.txt`; unmodified font |
| Qt 6.10.2 | LGPLv3 for the selected open-source modules, with separately licensed bundled components | Package `licenses/Qt-LICENSE.txt`; source/modules and delivery obligations must match the actual deployed Qt build |
| MinGW GCC / libstdc++ / libgcc | GPL with the applicable GCC Runtime Library Exception | Package `licenses/GCC-COPYING3.txt`, `GCC-COPYING3.LIB.txt`, `GCC-COPYING.RUNTIME.txt` |
| MinGW-w64 / winpthreads | Their upstream notices | Package `licenses/MinGW-w64-COPYING.txt`, `winpthreads-COPYING.txt` |

SumatraPDF and Obsidian are separately installed applications invoked through
their external interfaces. Their programs are not included in this repository
or the Pinloom package. Their names identify interoperability, not endorsement.
The optional SuiteApp SDK/runtime is separately supplied; this project's license
does not grant rights to redistribute it.

See [asset provenance](docs/ASSET-PROVENANCE.md) for application images. Pending
entries must be resolved before public redistribution of those assets.

Qt users retain the rights granted by its license, including modification and
relinking for that purpose. Before distributing public binaries, the publisher
must provide corresponding library sources and any required installation
information for the actual Qt build. A license text or upstream link alone is
not confirmation that those obligations have been met. See
https://www.qt.io/development/open-source-lgpl-obligations and the outstanding
[public-release review](docs/PUBLIC-RELEASE-REVIEW.md).
