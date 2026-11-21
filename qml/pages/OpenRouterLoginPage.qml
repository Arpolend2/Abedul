import QtQuick 2.0
import Sailfish.Silica 1.0
import ru.auroraos.WebView 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property var client

    Component.onDestruction: if (client && client.busy) client.cancel()

    Column {
        id: header
        width: parent.width
        PageHeader { title: qsTr("Sign in to OpenRouter") }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            visible: client && (client.busy || client.error.length > 0)
            wrapMode: Text.Wrap
            color: client && client.error.length ? Theme.errorColor : Theme.secondaryColor
            text: client && client.error.length ? client.error : qsTr("Waiting for OpenRouter…")
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: client && client.error.length > 0
            enabled: client && !client.busy
            text: qsTr("Try again")
            onClicked: {
                client.beginSignIn()
                if (client.authorizationUrl.length)
                    browser.url = client.authorizationUrl
            }
        }
    }
    WebView {
        id: browser
        objectName: "openRouterBrowser"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        url: client ? client.authorizationUrl : ""
        TouchInput {}
        KeyboardInput { enabled: true }
        LoadRequestExtension {
            objectName: "openRouterNavigation"
            enabled: true
            function beforeUrlLoad(url, userGesture, isRedirect) {
                return !client || !client.handleAuthorizationUrl(url.url)
            }
        }
    }
    Connections {
        target: page.client
        onAuthenticatedChanged: if (page.client.authenticated) pageStack.pop()
    }
}
