# Abedul

An independent native Overleaf client for Aurora OS.

- The interface is available in English and Russian, following the system language.
- Settings include an About section with the source repository and third-party references.
- Sign in to Overleaf Cloud or a self-hosted server, create projects, browse their folders, and upload multiple files, rename or delete files. The project-list toolbar provides project creation, refresh, archive and trash views, server selection, and settings. Existing files require confirmation before replacement.
- Move projects to the trash, restore them, or permanently delete owned projects from the trash. Local drafts are retained.
- On first opening a project, the editor opens the server's main document; if none is set, it uses a root `main.tex` or the sole `.tex` file when unambiguous. Previously opened files and existing drafts are preserved.
- Project lists, file trees, previously opened server files, drafts and the last compiled PDF are retained separately for each server and account. The last account's cached projects open immediately while the app reconnects and refreshes in the background. Session cookies are saved unencrypted in an app-private directory, separately for each account; passwords are not stored. Sign in again once after upgrading to save a session. If reconnection fails, an error is shown and cached files remain available. Offline files are read-only and may be out of date; files must be opened online after this feature is installed to be cached. Older unscoped drafts are retained separately and are not automatically assigned to an account.
- Delete a saved account to remove its session, cached files and account-scoped drafts; shared browser cookies are retained. Delete a saved server to remove its accounts and browser cookies as well. Domain cookies may also be used by other servers.
- Save and compile LaTeX with a PDF preview and two-way SyncTeX navigation.
- On phones and in portrait orientation, the editor and PDF use separate pages with the native forward/back gestures; landscape tablets show them side by side.
- Use Ctrl+S to save and compile, Ctrl+Y to redo an editor change, or Ctrl++ / Ctrl+- and Ctrl+mouse wheel to zoom the editor or PDF.
- Open Reverso from the editor toolbar to translate text and view usage examples. The default direction is Russian to English; choose other languages in the popup. Ctrl+R translates selected plain text, or the word at the cursor when nothing is selected, in the reverse direction and shows the result near the cursor. LaTeX math, commands, comments and literal code are not submitted. Only the submitted or selected text or word is sent to Reverso.
- Create a Wolfram|Alpha Full Results API AppID via the [Wolfram|Alpha API page](https://products.wolframalpha.com/api) by registering a free Wolfram ID and creating an AppID. The free non-commercial plan includes up to 2,000 requests per month. Enter the AppID in Settings, then press Ctrl+W while the cursor is inside LaTeX math (`$...$`, `\(...\)`, `\[...\]`, `$$...$$`, or a supported equation environment) to query it. Labels and comments are removed from equation environments, as are top-level alignment markers in `align` and `eqnarray`. Results appear in a compact popup beside the cursor. The calculator icon in the editor toolbar also opens a field for entering a formula separately; press Enter or Solve to query it. Only the requested formula is sent; the AppID is stored in owner-only app storage without encryption.
- Find and replace literal, case-sensitive text in the current document from the editor toolbar or with Ctrl+F. Use the up and down arrow keys in the search fields to navigate matches. Replacement requires a nonempty replacement field.
- Local LaTeX hints appear as you type commands; press Tab to accept them. On their own line, `\eqn`, `\equ`, `\tab` and `\fig` expand into blocks.
- The AI assistant is off by default. Enable it in Settings and sign in to OpenRouter in the in-app browser to request an AI autocomplete suggestion with Ctrl+I; ordinary LaTeX autocomplete remains available, and Tab accepts the current suggestion. AI autocomplete can insert text or replace a uniquely identified fragment at the cursor, shown before acceptance. Open the generation popup from the editor toolbar or Ctrl+Shift+I, review the proposed insertion or replacement and apply it explicitly. A generation replacement is offered only if its original text occurs exactly once in the file. Turning off the assistant hides AI features without affecting ordinary autocomplete. Both operations use the free Particle-1 model. After HTTP 429 they check the daily free-model quota and, if it remains available, retry with increasing delays and respect Retry-After; autocomplete also rotates through other free models on provider rate-limit errors. Retries continue until success, cancellation, quota exhaustion, or another error, and may take a long time. Each autocomplete model request has an eight-second timeout. AI editing sends the entire open file and the request to OpenRouter and a model provider; autocomplete sends up to 512 characters before and 128 after the cursor. The API key is saved unencrypted in an app-private file, like the saved Overleaf session; signing out removes the local key but does not revoke it on OpenRouter.
- The cover shows recent edits or the project list. Open the cached PDF in another app or download a ZIP of the saved server project to Downloads from the cover or editor toolbar.
- Invite collaborators, manage their access and pending invitations, and view project history, comments and chat where the server supports them. Sharing management requires project administrator access. Pull down to refresh the project list, project files, main-document list and collaboration pages. Chat and comments require manual refresh; creating comments and restoring history are not supported.
- The Sci-Hub toolbar button opens a DOI or article URL in the external browser. Its full HTTPS base address can be changed in Settings; the entered identifier is sent to that address.
- Drafts are stored locally; conflicting server changes stop a save. Before renaming or deleting an open document or its folder, the document is saved to the server; other unsaved drafts still block the operation. Binary files open as temporary copies; changes to these copies are not uploaded. Upload checks for existing names, but a concurrent upload by another user may still replace a file.
- Live collaboration, server-side sign-out and secure credential storage are not implemented.
- SyncTeX uses the compiled server snapshot and does not support pages with a non-default crop box or offset media box.

## Build

Run `git submodule update --init --recursive` and build `CMakeLists.txt` with Aurora SDK 5.2.1. PDFium and libjpeg-turbo are downloaded during the build; source archives must include `vendor/PdfViewer`.

The package identifier is `ru.arpolend.abedul`. It installs separately from the former `ru.auroraos.OverleafClient` package; accounts, settings, caches and drafts are not migrated.

## License

Abedul's original code is licensed under the [WTFPL, version 2](LICENSE). Third-party code remains under its respective licenses, including the BSD-3-Clause-licensed `qml/pages/SyncPdfView.qml` and PDF Viewer.

## Third-party code

[PDF Viewer](https://gitlab.com/omprussia/examples/PdfViewer) (AmberPDF and adapted QML), [PDFium binaries](https://github.com/bblanchon/pdfium-binaries), [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo), and the [RheniumBeta](https://gitlab.com/Danyok/rheniumbeta) icon (BSD-3-Clause; see `licenses/RheniumBeta-BSD-3-Clause.txt`). This software is based in part on the work of the Independent JPEG Group.
