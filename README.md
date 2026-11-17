# Abedul

An independent native Overleaf client for Aurora OS.

- The interface is available in English and Russian, following the system language.
- Sign in to Overleaf Cloud or a self-hosted server, browse projects and edit files.
- Save and compile LaTeX with a PDF preview and two-way SyncTeX navigation.
- Use Ctrl+S to save and compile, or Ctrl++ / Ctrl+- and Ctrl+mouse wheel to zoom the editor or PDF.
- Local LaTeX hints appear as you type commands; press Tab to accept them. On their own line, `\eqn`, `\equ`, `\tab` and `\fig` expand into blocks.
- The cover shows recent edits or the project list. Open the cached PDF in another app or download a ZIP of the saved server project to Downloads from the cover or editor menu.
- Drafts are stored locally; conflicting server changes stop a save. Binary files open as temporary copies and are not uploaded.
- The project file list is read-only. Live collaboration, sign-out and secure credential storage are not implemented.
- SyncTeX uses the compiled server snapshot and does not support pages with a non-default crop box or offset media box.

## Build

Run `git submodule update --init --recursive` and build `CMakeLists.txt` with Aurora SDK 5.2.1. PDFium and libjpeg-turbo are downloaded during the build; source archives must include `vendor/PdfViewer`.

## Third-party code

[PDF Viewer](https://gitlab.com/omprussia/examples/PdfViewer) (AmberPDF and adapted QML), [PDFium binaries](https://github.com/bblanchon/pdfium-binaries), and [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo). This software is based in part on the work of the Independent JPEG Group.
