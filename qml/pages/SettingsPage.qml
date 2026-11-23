import QtQuick 2.0
import Sailfish.Silica 1.0
import Abedul 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string aiSettingError: ""
    property string sciHubSettingError: ""
    property string aboutLinkError: ""
    property string wolframLinkError: ""
    OpenRouterClient { id: aiClient; objectName: "settingsAiClient" }
    WolframAlphaClient { id: wolframAlphaClient; objectName: "settingsWolframAlphaClient" }

    SilicaFlickable {
        objectName: "settingsFlickable"
        anchors.fill: parent
        contentHeight: content.height
        Column {
            id: content
            objectName: "settingsContent"
            width: parent.width
            PageHeader { title: qsTr("Settings") }
            TextSwitch {
                objectName: "pdfOnLeftSetting"
                width: parent.width
                text: qsTr("PDF on the left")
                description: qsTr("Swap the editor and PDF when displayed side by side.")
                checked: overleafClient.pdfOnLeft
                onClicked: overleafClient.pdfOnLeft = checked
            }
            Slider {
                objectName: "editorFontScaleSetting"
                width: parent.width
                label: qsTr("Editor font size")
                minimumValue: 70
                maximumValue: 160
                stepSize: 10
                value: overleafClient.editorFontScale
                valueText: Math.round(value) + "%"
                onValueChanged: overleafClient.editorFontScale = Math.round(value)
            }
            Label {
                objectName: "zoomShortcutHint"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("To zoom the editor or PDF, point at the desired area and use Ctrl++ / Ctrl+- or Ctrl + mouse wheel.")
            }
            Label {
                objectName: "saveShortcutHint"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("Press Ctrl+S to save and compile.")
            }
            SectionHeader { text: qsTr("WolframAlpha") }
            TextField {
                id: wolframAlphaAppId
                objectName: "wolframAlphaAppIdSetting"
                width: parent.width
                label: qsTr("WolframAlpha AppID")
                placeholderText: qsTr("Enter your AppID")
                echoMode: TextInput.Password
                text: wolframAlphaClient.appId
                Connections {
                    target: wolframAlphaAppId._editor
                    onEditingFinished: wolframAlphaClient.appId = wolframAlphaAppId.text
                }
            }
            Label {
                objectName: "wolframAlphaApiHint"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                textFormat: Text.RichText
                text: qsTr("A Full Results API AppID is required. Register a free Wolfram ID and create an AppID on the developer portal. %1")
                      .arg('<a href="https://products.wolframalpha.com/api">' + qsTr("Get a WolframAlpha AppID") + '</a>')
                onLinkActivated: page.wolframLinkError = Qt.openUrlExternally(link) ? "" : qsTr("Could not open the link.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                text: qsTr("Press Ctrl+W in a LaTeX equation to send it to WolframAlpha.")
            }
            Label {
                objectName: "wolframAlphaSettingError"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.wolframLinkError.length > 0 || wolframAlphaClient.error.length > 0
                wrapMode: Text.Wrap
                color: Theme.errorColor
                text: page.wolframLinkError.length > 0 ? page.wolframLinkError : wolframAlphaClient.error
            }
            SectionHeader { text: qsTr("Sci-Hub") }
            TextField {
                id: sciHubAddress
                objectName: "sciHubAddressSetting"
                width: parent.width
                label: qsTr("Sci-Hub address")
                placeholderText: "https://sci-hub.ru"
                text: overleafClient.sciHubAddress
                inputMethodHints: Qt.ImhUrlCharactersOnly
                Connections {
                    target: sciHubAddress._editor
                    onEditingFinished: {
                        if (overleafClient.setSciHubAddress(sciHubAddress.text))
                            page.sciHubSettingError = ""
                        else
                            page.sciHubSettingError = qsTr("Enter a valid HTTPS address without credentials, query or fragment.")
                    }
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.sciHubSettingError.length > 0
                wrapMode: Text.Wrap
                color: Theme.errorColor
                text: page.sciHubSettingError
            }
            SectionHeader { text: qsTr("AI assistant") }
            TextSwitch {
                objectName: "aiEnabledSetting"
                width: parent.width
                text: qsTr("Enable AI assistant")
                description: qsTr("Normal autocomplete stays on. Press Ctrl+I for an AI suggestion, then Tab to accept.")
                checked: aiClient.aiEnabled
                onClicked: {
                    if (!checked) {
                        aiClient.cancel()
                    }
                    aiClient.aiEnabled = checked
                    page.aiSettingError = aiClient.aiEnabled === checked ? "" : aiClient.error
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: aiClient.aiEnabled
                wrapMode: Text.Wrap
                text: qsTr("Open AI generation from the editor menu or with Ctrl+Shift+I.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: aiClient.aiEnabled
                wrapMode: Text.Wrap
                text: qsTr("AI editing sends the whole open file and your request to OpenRouter and an AI provider. Autocomplete sends nearby document text. Free models have usage limits. The saved API key is stored in a private app file, not encrypted.")
            }
            Button {
                objectName: "aiSignIn"
                anchors.horizontalCenter: parent.horizontalCenter
                visible: aiClient.aiEnabled && !aiClient.authenticated
                enabled: !aiClient.busy
                text: qsTr("Sign in to OpenRouter")
                onClicked: {
                    aiClient.beginSignIn()
                    if (aiClient.authorizationUrl.length)
                        pageStack.push(Qt.resolvedUrl("OpenRouterLoginPage.qml"), { "client": aiClient })
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: aiClient.aiEnabled && aiClient.authenticated
                enabled: !aiClient.busy
                text: qsTr("Sign out of OpenRouter")
                onClicked: aiClient.signOut()
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.aiSettingError.length > 0
                         || (aiClient.aiEnabled && (aiClient.busy || aiClient.error.length > 0))
                wrapMode: Text.Wrap
                color: page.aiSettingError.length || aiClient.error.length ? Theme.errorColor : Theme.secondaryColor
                text: page.aiSettingError.length ? page.aiSettingError
                      : aiClient.error.length ? aiClient.error : qsTr("Waiting for OpenRouter…")
            }
            SectionHeader { text: qsTr("About") }
            Item {
                id: aboutHeader
                objectName: "aboutHeader"
                width: parent.width
                height: Math.max(aboutIcon.height, aboutText.height)
                Image {
                    id: aboutIcon
                    objectName: "aboutIcon"
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(2 * Theme.iconSizeLarge,
                                    Math.max(0, parent.width - 2 * Theme.horizontalPageMargin - Theme.paddingMedium) * 0.35)
                    height: width
                    source: "../icons/abedul-about.png"
                    fillMode: Image.PreserveAspectFit
                }
                Item {
                    id: aboutText
                    x: aboutIcon.x + aboutIcon.width + Theme.paddingMedium
                    width: Math.max(0, parent.width - x - Theme.horizontalPageMargin)
                    height: aboutVersion.height + Theme.paddingSmall + aboutDescription.height
                    anchors.verticalCenter: parent.verticalCenter
                    Label {
                        id: aboutVersion
                        objectName: "aboutVersion"
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "Abedul " + appVersion
                    }
                    Label {
                        id: aboutDescription
                        objectName: "aboutDescription"
                        anchors.top: aboutVersion.bottom
                        anchors.topMargin: Theme.paddingSmall
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        text: qsTr("Abedul is a unique 4-in-1 mathematical IDE for working on scientific papers. Edit Overleaf documents, translate text with Reverso, check formulas with WolframAlpha and find publications through Sci-Hub without interrupting your work.")
                    }
                }
            }
            BackgroundItem {
                objectName: "aboutLinks"
                property string url: "https://github.com/Arpolend2/Abedul"
                width: parent.width
                onClicked: page.aboutLinkError = Qt.openUrlExternally(url) ? "" : qsTr("Could not open the link.")
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Source repository")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }
            SectionHeader { text: qsTr("What's new in this version") }
            Label {
                objectName: "aboutReleaseNotes"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("- Updated interface using the Aurora OS 5 design language.\n- AI suggestions and text generation with OpenRouter.\n- Translation with Reverso and calculations with WolframAlpha.\n- Sci-Hub links and improved project history and access management.")
            }
            SectionHeader {
                objectName: "aboutReferencesHeading"
                text: qsTr("References and third-party components")
            }
            Repeater {
                model: [
                    { name: "Overleaf for VS Code", url: "https://github.com/augustiniusliang/overleaf-for-vscode" },
                    { name: "PDF Viewer (AmberPDF)", url: "https://gitlab.com/omprussia/examples/PdfViewer" },
                    { name: "PDFium", url: "https://github.com/bblanchon/pdfium-binaries" },
                    { name: "libjpeg-turbo", url: "https://github.com/libjpeg-turbo/libjpeg-turbo" },
                    { name: "RheniumBeta", url: "https://gitlab.com/Danyok/rheniumbeta" }
                ]
                delegate: BackgroundItem {
                    objectName: "aboutReferenceLink"
                    property string url: modelData.url
                    width: parent.width
                    onClicked: page.aboutLinkError = Qt.openUrlExternally(url) ? "" : qsTr("Could not open the link.")
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        truncationMode: TruncationMode.Fade
                        color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.aboutLinkError.length > 0
                wrapMode: Text.Wrap
                color: Theme.errorColor
                text: page.aboutLinkError
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("This software is based in part on the work of the Independent JPEG Group.")
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
