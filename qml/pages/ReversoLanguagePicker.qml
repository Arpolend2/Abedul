import QtQuick 2.5
import Sailfish.Silica 1.0

ComboBox {
    id: picker
    leftMargin: Theme.paddingSmall
    rightMargin: Theme.paddingSmall
    property var languages: [
        { code: "ru", name: qsTr("Russian") },
        { code: "en", name: qsTr("English") },
        { code: "de", name: qsTr("German") },
        { code: "fr", name: qsTr("French") },
        { code: "es", name: qsTr("Spanish") },
        { code: "it", name: qsTr("Italian") }
    ]
    readonly property string languageCode: currentIndex >= 0 && currentIndex < languages.length
                                           ? languages[currentIndex].code : ""
    menu: ContextMenu {
        Repeater {
            model: picker.languages
            MenuItem { text: modelData.name }
        }
    }
}
