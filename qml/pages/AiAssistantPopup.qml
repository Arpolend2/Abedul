import QtQuick 2.0
import Sailfish.Silica 1.0

Rectangle {
    id: page
    objectName: "aiGeneratorPopup"
    property var client
    property var editorPage
    property color textColor: Theme.primaryColor
    property color backgroundColor: "black"
    property bool showing: false
    property string originalText: ""
    property int originalStart: 0
    property int originalEnd: 0
    property string originalFile: ""
    property string resultText: ""
    property string replacedText: ""
    property bool proposalReady: false
    property bool replacement: false
    property string insertError: ""
    visible: showing
    color: backgroundColor
    border.color: textColor
    radius: Theme.paddingLarge
    width: Math.min(parent.width, Theme.itemSizeLarge * 5)
    height: Math.min(parent.height, Theme.itemSizeLarge * 8,
                     content.height + 2 * Theme.paddingLarge)
    Component.onDestruction: if (client && client.busy) client.cancel()

    function open() {
        if (!showing) {
            originalText = ""
            originalStart = 0
            originalEnd = 0
            originalFile = ""
            resultText = ""
            replacedText = ""
            proposalReady = false
            replacement = false
            insertError = ""
            instruction.text = ""
            showing = true
        }
        if (client.authenticated)
            instruction.forceActiveFocus()
    }

    function close() {
        if (client && client.busy)
            client.cancel()
        showing = false
    }

    function receiveProposal(text) {
        page.proposalReady = false
        page.insertError = ""
        var response
        try {
            var json = text.trim()
            if (/^```(?:json)?\s*[\r\n]/i.test(json) && /```$/.test(json))
                json = json.replace(/^```(?:json)?\s*[\r\n]/i, "").replace(/```\s*$/, "")
            response = JSON.parse(json)
        } catch (e) {
            page.insertError = qsTr("The AI returned an invalid edit. Try again.")
            return
        }
        if (!response || typeof response !== "object" || typeof response.new_text !== "string"
                || (response.action !== "replace" && response.action !== "insert")) {
            page.insertError = qsTr("The AI could not identify an edit. Describe the change more precisely.")
            return
        }
        if (response.action === "replace") {
            if (typeof response.old_text !== "string" || !response.old_text.length) {
                page.insertError = qsTr("The AI returned an invalid edit. Try again.")
                return
            }
            var start = page.originalText.indexOf(response.old_text)
            if (start < 0 || page.originalText.indexOf(response.old_text, start + 1) >= 0) {
                page.insertError = qsTr("The AI could not uniquely locate the original text. Describe the change more precisely.")
                return
            }
            page.originalStart = start
            page.originalEnd = start + response.old_text.length
            page.replacedText = response.old_text
            page.replacement = true
        } else {
            if (!response.new_text.length) {
                page.insertError = qsTr("The AI returned an invalid edit. Try again.")
                return
            }
            page.originalStart = page.originalEnd
            page.replacedText = ""
            page.replacement = false
        }
        page.resultText = response.new_text
        page.proposalReady = true
    }

    MouseArea { anchors.fill: parent }
    SilicaFlickable {
        objectName: "aiGeneratorFlickable"
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge
        contentHeight: content.height
        clip: true
        Column {
            id: content
            width: parent.width
            Item {
                width: parent.width
                height: Theme.itemSizeSmall
                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.paddingMedium
                    text: qsTr("AI assistant")
                    color: page.textColor
                    truncationMode: TruncationMode.Fade
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: !client.authenticated
                wrapMode: Text.Wrap
                text: qsTr("Sign in to OpenRouter in Settings first.")
            }
            TextArea {
                id: instruction
                objectName: "aiPrompt"
                width: parent.width
                visible: client.authenticated
                label: qsTr("Describe the LaTeX to generate or change")
                placeholderText: qsTr("For example: change the table to three columns")
            }
            Button {
                objectName: "aiGenerate"
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, Theme.buttonWidthMedium)
                visible: client.authenticated
                enabled: !client.busy && instruction.text.trim().length > 0
                text: qsTr("Generate")
                onClicked: {
                    page.originalText = editorPage.aiDocumentText()
                    page.originalStart = editorPage.aiCursorPosition()
                    page.originalEnd = page.originalStart
                    page.originalFile = editorPage.currentFile
                    page.resultText = ""
                    page.replacedText = ""
                    page.proposalReady = false
                    page.insertError = ""
                    client.requestEdit(instruction.text, page.originalText)
                }
            }
            Label {
                objectName: "aiRequestStatus"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: client.busy || client.error.length > 0
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: client.error.length ? Theme.errorColor : Theme.secondaryColor
                text: client.error.length ? client.error : qsTr("Waiting for OpenRouter…")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.proposalReady
                wrapMode: Text.Wrap
                text: page.replacement ? qsTr("Original text to replace:") : qsTr("Insert at cursor:")
            }
            Label {
                objectName: "aiOriginalText"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.proposalReady && page.replacement
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: page.replacedText
                font.family: "monospace"
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.proposalReady && page.replacement
                text: qsTr("Replacement:")
            }
            Label {
                objectName: "aiGeneratedText"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.proposalReady
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: page.resultText.length ? page.resultText : qsTr("(remove text)")
                font.family: "monospace"
            }
            Button {
                objectName: "aiInsert"
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, Theme.buttonWidthMedium)
                visible: page.proposalReady
                text: page.replacement ? qsTr("Apply replacement") : qsTr("Insert into document")
                onClicked: {
                    if (editorPage.applyAiGeneration(page.originalFile, page.originalText,
                                                     page.originalStart, page.originalEnd, page.resultText))
                        page.close()
                    else
                        page.insertError = qsTr("The document changed or cannot be edited. Generate the text again.")
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.insertError.length > 0
                color: Theme.errorColor
                wrapMode: Text.Wrap
                text: page.insertError
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
    Connections {
        target: page.client
        onResultReady: if (page.showing) page.receiveProposal(text)
    }
}
