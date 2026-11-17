import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property bool restoringServer: overleafClient.rememberedServer.length > 0
    property bool restoreStarted: false
    function openServer(server) {
        if (overleafClient.setServer(server))
            pageStack.push(Qt.resolvedUrl("WebLoginPage.qml"))
    }
    onStatusChanged: {
        if (status === PageStatus.Active && restoreStarted)
            restoringServer = false
    }
    Component.onCompleted: {
        if (overleafClient.rememberedServer.length)
            restoreTimer.start()
    }
    Timer {
        id: restoreTimer
        interval: 0
        onTriggered: {
            page.restoreStarted = true
            if (overleafClient.setServer(overleafClient.rememberedServer))
                pageStack.push(Qt.resolvedUrl("WebLoginPage.qml"), { "restoringSession": true })
            else
                page.restoringServer = false
        }
    }
    BusyIndicator {
        anchors.centerIn: parent
        size: BusyIndicatorSize.Large
        running: page.restoringServer
        visible: running
    }
    SilicaFlickable {
        anchors.fill: parent
        visible: !page.restoringServer
        contentHeight: column.height
        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge
            PageHeader { title: "Abedul" }
            TextField {
                id: address
                width: parent.width
                label: qsTr("Server address")
                text: overleafClient.rememberedServer.length
                      ? overleafClient.rememberedServer : "https://www.overleaf.com"
                inputMethodHints: Qt.ImhUrlCharactersOnly
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Sign in")
                onClicked: page.openServer(address.text)
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: qsTr("Previous servers")
                visible: overleafClient.rememberedServers.length > 0
            }
            Repeater {
                objectName: "previousServers"
                model: overleafClient.rememberedServers
                delegate: Button {
                    objectName: "previousServer-" + index
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    text: modelData
                    onClicked: page.openServer(modelData)
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                text: overleafClient.error
                visible: text.length > 0
            }
        }
        VerticalScrollDecorator {}
    }
}
