import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height
        Column {
            id: content
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
            SectionHeader { text: qsTr("About") }
            Label {
                objectName: "aboutDescription"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                text: qsTr("Abedul is an independent native Overleaf client for Aurora OS.")
            }
            Label {
                objectName: "aboutVersion"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: qsTr("Version: %1").arg(appVersion)
            }
            Label {
                objectName: "aboutLinks"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                textFormat: Text.RichText
                text: '<a href="https://github.com/Arpolend2/Abedul">' + qsTr("Source repository") + '</a>'
                onLinkActivated: Qt.openUrlExternally(link)
            }
            Label {
                objectName: "aboutReferencesHeading"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                text: qsTr("References and third-party components:")
            }
            Label {
                objectName: "aboutReferences"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                textFormat: Text.RichText
                text: '<a href="https://github.com/augustiniusliang/overleaf-for-vscode">Overleaf for VS Code</a><br>'
                    + '<a href="https://gitlab.com/omprussia/examples/PdfViewer">PDF Viewer (AmberPDF)</a><br>'
                    + '<a href="https://github.com/bblanchon/pdfium-binaries">PDFium</a><br>'
                    + '<a href="https://github.com/libjpeg-turbo/libjpeg-turbo">libjpeg-turbo</a>'
                onLinkActivated: Qt.openUrlExternally(link)
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
