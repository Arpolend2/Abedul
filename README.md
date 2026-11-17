# Abedul

This is an early, incomplete native Aurora OS client for Overleaf Cloud and
self-hosted servers. The current build contains browser sign-in and a project
list and an early editor. Project documents are opened through Overleaf's
real-time protocol, without downloading a ZIP snapshot. Edits are saved locally
as drafts. "Save and compile" (or Ctrl+S) sends the current file to Overleaf
and compiles only after the server confirms the update. The current `.tex`
file becomes the main document; for other files, the last opened `.tex` file
is used. LaTeX commands, references, environments, comments,
math and verbatim text, as well as common BibTeX syntax, are highlighted.
The original per-project draft is preserved separately.

The last opened project is highlighted in the project list and available via
forward navigation. Its last opened source file is reopened on return.
Pull down on the fixed editor header to access project actions.
With a mouse, use the wheel over the header to open the pulley menu; elsewhere
the editor and PDF retain their own scrolling. Mouse back and forward buttons
follow the same page-navigation rules as the edge gestures.
The settings page can swap the PDF and editor when they are side by side and
adjust the editor font size.
Ctrl++, Ctrl+- and Ctrl+mouse wheel adjust the scale of the pane under the
mouse pointer. On touch devices, the active editor or PDF is used instead.
The PDF preview is recreated after each compilation, resetting its zoom and
scroll position. An activity indicator shows when saving and compilation are
in progress; the server does not report a percentage.

If the server document has changed since the draft was opened, saving is
stopped to avoid overwriting it and the local draft is retained. Old drafts
without a recorded server base cannot be sent unless they already match the
server. Copy any local changes before resolving such a conflict manually.
If the connection drops during submission, the server may have applied the
change; retrying checks the server text before sending again. The project file
list is read-only; file management and live collaboration are not implemented.

Opening documents currently requires a real-time endpoint on the same origin
as the configured server. The client uses undocumented Overleaf endpoints, which may change between
server releases. HTTPS server origins without a path are currently supported.
Sign-in happens in Aurora WebView; the session cookies for the configured
server are copied into memory for API requests. WebView may persist browser
data in its own application storage; this build does not provide sign-out
or secure credential storage. Do not use a shared device without considering
that limitation.

After sign-in the app automatically continues when the browser reaches the
project list. Previously selected servers are listed on the sign-in page and
the last selected server is reopened at startup; existing browser cookies are
reused when available. The session can
expire or fail to persist across restarts, in which case the sign-in page is
shown. While the saved session is being restored, the app shows a loading
indicator instead of the sign-in page.
The button remains available if automatic detection fails. Use the project
list menu to select a different server.

## Building

Open `CMakeLists.txt` in Aurora IDE and build with an Aurora 5.2.1 SDK kit.
The app requires Aurora WebView (Chromium). Enable `BUILD_TESTS` to build
the focused Qt tests. The RPM spec must use LF line endings: CRLF breaks
Aurora SDK's generated shell script.

## References and acknowledgements

- [overleaf-for-vscode](https://github.com/augustiniusliang/overleaf-for-vscode)
  was a reference for the Overleaf real-time protocol.
- [Overleaf](https://github.com/overleaf/overleaf) and
  [Overleaf Workshop](https://github.com/overleaf-workshop/Overleaf-Workshop)
  were consulted for server and compilation behavior.
- [AmberPDF](https://hub.mos.ru/auroraos/libraries/amberpdf) and the
  [AmberPDF QML plugin](https://hub.mos.ru/auroraos/libraries/amberpdf-qml-plugin)
  provide PDF rendering. Their PDF Viewer example was consulted.
- [Aurora WebView](https://developer.auroraos.ru/doc/5.2.1/software_development/reference/web_and_content/webview_chromium/namespaces/namespaceAurora/namespaceAurora_1_1WebView/WebViewItem),
  its [CookieManager](https://developer.auroraos.ru/doc/5.2.1/software_development/reference/web_and_content/webview_chromium/namespaces/namespaceAurora/namespaceAurora_1_1WebView/CookieManager),
  and the [Aurora icon guidelines](https://developer.auroraos.ru/doc/5.2.1/ui_kit/icons)
  were used as platform references.
- [AuroOo](https://github.com/Arpolend2/AuroOo) provided a reference for mouse
  navigation and pulley-menu behavior.

For syntax highlighting, [shiki-tex](https://github.com/fourdim/shiki-tex),
[vscode-latex](https://github.com/mathematic-inc/vscode-latex),
[TextMate](https://macromates.com/), and
[ChkTeX](https://www.nongnu.org/chktex/) were considered but are not integrated.
