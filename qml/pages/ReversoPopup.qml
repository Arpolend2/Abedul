import QtQuick 2.5
import Sailfish.Silica 1.0
import Abedul 1.0

Rectangle {
    id: root
    objectName: "reversoPopup"
    property color textColor: Theme.primaryColor
    property color backgroundColor: "black"
    property bool showing: false
    property bool submitted: false
    property bool reverseDirection: false
    property string translatedWord: ""
    property real maximumWidth: parent.width
    property real maximumHeight: parent.height
    readonly property real preferredHeight: Math.min(Theme.itemSizeLarge * 8,
                                                     contents.height + 2 * Theme.paddingLarge)
    property alias client: reverso
    readonly property bool darkTheme: Theme.colorScheme === Theme.LightOnDark
    readonly property color surfaceColor: darkTheme ? "#222224" : "#f1f1f1"
    readonly property color lineColor: darkTheme ? "#454548" : "#c6c6c6"
    readonly property color accentColor: darkTheme ? "#a4d3ff" : "#185a92"
    visible: showing
    color: backgroundColor
    border.color: textColor
    radius: Theme.paddingLarge
    width: Math.min(maximumWidth, Theme.itemSizeLarge * 5)
    height: Math.min(maximumHeight, preferredHeight)

    function open() {
        if (reverseDirection)
            submitted = false
        reverseDirection = false
        showing = true
        input.forceActiveFocus()
    }
    function close() {
        reverso.cancel()
        showing = false
    }
    function submit() {
        if (reverso.busy)
            return
        reverseDirection = false
        submitted = true
        reverso.request(input.text, sourceLanguage.languageCode, targetLanguage.languageCode)
    }
    function translateText(text) {
        reverso.cancel()
        translatedWord = text
        reverseDirection = true
        submitted = true
        showing = true
        reverso.request(translatedWord, targetLanguage.languageCode, sourceLanguage.languageCode)
    }
    function swapLanguages() {
        if (reverso.busy)
            return
        var previous = sourceLanguage.currentIndex
        sourceLanguage.currentIndex = targetLanguage.currentIndex
        targetLanguage.currentIndex = previous
        submitted = false
    }
    ReversoClient {
        id: reverso
        objectName: "editorReversoClient"
    }
    MouseArea { anchors.fill: parent }
    SilicaFlickable {
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge
        contentHeight: contents.height
        clip: true
        Column {
            id: contents
            width: parent.width
            spacing: Theme.paddingSmall
            Row {
                id: languageRow
                width: parent.width
                visible: !root.reverseDirection
                ReversoLanguagePicker {
                    id: sourceLanguage
                    objectName: "reversoSourceLanguage"
                    width: Math.max(0, (parent.width - Theme.itemSizeSmall) / 2)
                    currentIndex: 0
                    enabled: !reverso.busy
                    onCurrentIndexChanged: if (root.showing) root.submitted = false
                }
                Button {
                    objectName: "reversoSwapLanguages"
                    width: Theme.itemSizeSmall
                    text: "⇄"
                    Accessible.name: qsTr("Swap languages")
                    enabled: !reverso.busy
                    onClicked: root.swapLanguages()
                }
                ReversoLanguagePicker {
                    id: targetLanguage
                    objectName: "reversoTargetLanguage"
                    width: Math.max(0, (parent.width - Theme.itemSizeSmall) / 2)
                    currentIndex: 1
                    enabled: !reverso.busy
                    onCurrentIndexChanged: if (root.showing) root.submitted = false
                }
            }
            Item {
                width: parent.width
                height: Theme.itemSizeSmall
                visible: root.reverseDirection
                Text {
                    objectName: "reversoWord"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.paddingSmall
                    anchors.rightMargin: Theme.paddingSmall
                    text: root.translatedWord
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: root.textColor
                    font.bold: true
                    font.pixelSize: Theme.fontSizeMedium
                }
            }
            Rectangle {
                objectName: "reversoInputContainer"
                width: parent.width
                height: input.height + 2 * Theme.paddingSmall
                visible: !root.reverseDirection
                radius: Theme.paddingSmall
                color: root.surfaceColor
                border.color: root.lineColor
                TextField {
                    id: input
                    objectName: "reversoInput"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.paddingSmall
                    placeholderText: qsTr("Enter text to translate")
                    readOnly: reverso.busy
                    onTextChanged: root.submitted = false
                    EnterKey.onClicked: root.submit()
                    Connections {
                        target: input._editor
                        onAccepted: root.submit()
                    }
                }
            }
            Button {
                objectName: "reversoSubmit"
                width: parent.width
                visible: !root.reverseDirection
                text: qsTr("Translate")
                enabled: !reverso.busy
                onClicked: root.submit()
            }
            Text {
                objectName: "reversoDirection"
                width: parent.width
                visible: root.submitted && root.reverseDirection
                text: qsTr("Reverse translation") + ": "
                      + (targetLanguage.currentItem ? targetLanguage.currentItem.text : "")
                      + " → "
                      + (sourceLanguage.currentItem ? sourceLanguage.currentItem.text : "")
                color: root.accentColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
            }
            Text {
                objectName: "reversoResult"
                width: parent.width
                visible: root.submitted
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: root.textColor
                font.pixelSize: Theme.fontSizeSmall
                text: (reverso.busy ? qsTr("Waiting for Reverso…") : "")
                      + (reverso.busy && reverso.error.length ? "\n" : "") + reverso.error
            }
            Text {
                width: parent.width
                visible: root.submitted && reverso.translations.length > 0
                text: qsTr("Translation")
                color: root.accentColor
                font.bold: true
                font.pixelSize: Theme.fontSizeSmall
            }
            Flow {
                id: translationList
                objectName: "reversoTranslations"
                width: parent.width
                spacing: Theme.paddingSmall
                visible: root.submitted && reverso.translations.length > 0
                Repeater {
                    model: reverso.translations
                    Rectangle {
                        objectName: "reversoTranslation"
                        width: Math.min(translationList.width, translationLabel.implicitWidth + 2 * Theme.paddingMedium)
                        height: translationLabel.height + 2 * Theme.paddingSmall
                        radius: Theme.paddingSmall
                        color: root.surfaceColor
                        border.color: root.lineColor
                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 2
                            color: root.accentColor
                        }
                        Text {
                            id: translationLabel
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Theme.paddingMedium
                            text: modelData
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: root.textColor
                            font.bold: true
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }
            }
            Text {
                width: parent.width
                visible: root.submitted && reverso.examples.length > 0
                text: qsTr("Examples in context")
                color: root.accentColor
                font.bold: true
                font.pixelSize: Theme.fontSizeSmall
            }
            Repeater {
                model: reverso.examples
                Rectangle {
                    objectName: "reversoExample"
                    width: contents.width
                    visible: root.submitted
                    height: exampleContents.height + 2 * Theme.paddingMedium
                    color: root.surfaceColor
                    border.color: root.lineColor
                    Column {
                        id: exampleContents
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.paddingMedium
                        spacing: Theme.paddingSmall
                        Text {
                            objectName: "reversoExampleSource"
                            width: parent.width
                            text: modelData.source
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: root.accentColor
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Text {
                            objectName: "reversoExampleTarget"
                            width: parent.width
                            text: "↳ " + modelData.target
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: root.textColor
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }
            }
        }
        VerticalScrollDecorator {}
    }
}
