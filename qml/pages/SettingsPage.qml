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
        }
        VerticalScrollDecorator {}
    }
}
