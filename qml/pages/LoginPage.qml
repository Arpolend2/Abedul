import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property bool restoringServer: (overleafClient.autoRestoreServer !== false && overleafClient.rememberedServer.length > 0)
                                   || (overleafClient.cachedAccounts && overleafClient.cachedAccounts.length > 0)
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
        if (restoringServer)
            restoreTimer.start()
    }
    Timer {
        id: restoreTimer
        interval: 0
        onTriggered: {
            page.restoreStarted = true
            var accounts = overleafClient.cachedAccounts || []
            if (accounts.length && overleafClient.openOfflineAccount(accounts[0].server, accounts[0].userId))
                pageStack.push(Qt.resolvedUrl("ProjectsPage.qml"), {})
            else if (overleafClient.autoRestoreServer !== false && overleafClient.rememberedServer.length
                     && overleafClient.setServer(overleafClient.rememberedServer))
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
                enabled: !overleafClient.busy && overleafClient.interactiveCookiePending !== true
                         && overleafClient.serverRemovalPending !== true
                onClicked: page.openServer(address.text)
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: qsTr("Saved accounts")
                visible: overleafClient.cachedAccounts && overleafClient.cachedAccounts.length > 0
            }
            Repeater {
                objectName: "savedAccounts"
                model: overleafClient.cachedAccounts || []
                delegate: Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    spacing: Theme.paddingSmall
                    Button {
                        objectName: "savedAccount"
                        width: parent.width - removeAccount.width - parent.spacing
                        text: modelData.label + " — " + modelData.server
                        enabled: !overleafClient.busy && overleafClient.interactiveCookiePending !== true
                                 && overleafClient.serverRemovalPending !== true
                        onClicked: {
                            if (overleafClient.openOfflineAccount(modelData.server, modelData.userId))
                                pageStack.push(Qt.resolvedUrl("ProjectsPage.qml"), {})
                        }
                    }
                    Button {
                        id: removeAccount
                        objectName: "removeSavedAccount"
                        preferredWidth: Theme.buttonWidthExtraSmall
                        text: qsTr("Delete")
                        enabled: !overleafClient.busy && overleafClient.interactiveCookiePending !== true
                                 && overleafClient.serverRemovalPending !== true
                        onClicked: pageStack.push(removeAccountDialog, {
                            "accountServer": modelData.server,
                            "accountId": modelData.userId,
                            "accountLabel": modelData.label
                        })
                    }
                }
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
                delegate: Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    spacing: Theme.paddingSmall
                    Button {
                        objectName: "previousServer-" + index
                        width: parent.width - removeServer.width - parent.spacing
                        text: modelData
                        enabled: !overleafClient.busy && overleafClient.interactiveCookiePending !== true
                                 && overleafClient.serverRemovalPending !== true
                        onClicked: page.openServer(modelData)
                    }
                    Button {
                        id: removeServer
                        objectName: "removeSavedServer"
                        preferredWidth: Theme.buttonWidthExtraSmall
                        text: qsTr("Delete")
                        enabled: !overleafClient.busy && overleafClient.interactiveCookiePending !== true
                                 && overleafClient.serverRemovalPending !== true
                        onClicked: pageStack.push(removeServerDialog, { "serverAddress": modelData })
                    }
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
    Component {
        id: removeAccountDialog
        Dialog {
            objectName: "removeAccountDialog"
            property string accountServer
            property string accountId
            property string accountLabel
            onAccepted: overleafClient.removeCachedAccount(accountServer, accountId)
            SilicaFlickable {
                anchors.fill: parent
                contentHeight: accountContents.height
                Column {
                    id: accountContents
                    width: parent.width
                    DialogHeader { title: qsTr("Delete account"); acceptText: qsTr("Delete") }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: qsTr("Delete %1 and its saved session, cached files and account drafts? Browser cookies are kept.").arg(accountLabel)
                    }
                }
                VerticalScrollDecorator {}
            }
        }
    }
    Component {
        id: removeServerDialog
        Dialog {
            objectName: "removeServerDialog"
            property string serverAddress
            onAccepted: overleafClient.requestServerRemoval(serverAddress)
            SilicaFlickable {
                anchors.fill: parent
                contentHeight: serverContents.height
                Column {
                    id: serverContents
                    width: parent.width
                    DialogHeader { title: qsTr("Delete server"); acceptText: qsTr("Delete") }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: qsTr("Delete %1, all its accounts, saved files, drafts and browser cookies? Shared domain cookies may also affect other servers.").arg(serverAddress)
                    }
                }
                VerticalScrollDecorator {}
            }
        }
    }
}
