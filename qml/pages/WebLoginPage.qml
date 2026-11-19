import QtQuick 2.0
import Sailfish.Silica 1.0
import ru.auroraos.WebView 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string message: ""
    property bool cookieRequestPending: false
    property bool cookieRequestAutomatic: false
    property bool automaticCookieDeferred: false
    property bool restoringSession: false

    function showLogin() {
        restoringSession = false
        restoreTimeout.stop()
    }

    function requestCookies(automatic) {
        if (cookieRequestPending || overleafClient.busy || overleafClient.authenticated)
            return
        if (overleafClient.backgroundCookiePending || overleafClient.interactiveCookiePending
                || overleafClient.serverRemovalPending) {
            if (automatic)
                automaticCookieDeferred = true
            return
        }
        automaticCookieDeferred = false
        cookieRequestPending = true
        cookieRequestAutomatic = automatic
        overleafClient.setInteractiveCookiePending(true)
        if (!CookieManager.retrieveUrlCookies(overleafClient.server + "/project", true)) {
            cookieRequestPending = false
            overleafClient.setInteractiveCookiePending(false)
            message = qsTr("Unable to read browser cookies.")
            if (automatic)
                showLogin()
        } else {
            cookieTimeout.restart()
        }
    }

    Column {
        id: header
        width: parent.width
        height: page.restoringSession ? 0 : implicitHeight
        visible: !page.restoringSession
        PageHeader { title: qsTr("Sign in") }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: overleafClient.busy ? qsTr("Loading projects…") : qsTr("Continue after sign-in")
            enabled: !overleafClient.busy && !page.cookieRequestPending
                     && !overleafClient.backgroundCookiePending && !overleafClient.interactiveCookiePending
                     && !overleafClient.serverRemovalPending
            onClicked: page.requestCookies(false)
        }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            color: Theme.errorColor
            text: page.message.length ? page.message : overleafClient.error
            visible: text.length > 0
        }
    }
    Timer {
        id: cookieTimeout
        interval: 10000
        onTriggered: {
            page.cookieRequestPending = false
            page.message = qsTr("Timed out reading browser cookies. Please try again.")
            page.showLogin()
        }
    }
    Timer {
        id: restoreTimeout
        interval: 45000
        onTriggered: {
            page.message = qsTr("Connecting took too long. Please try signing in again.")
            page.showLogin()
        }
    }
    Component.onCompleted: if (restoringSession) restoreTimeout.start()
    AutoLogin {
        webView: loginWebView
        client: overleafClient
        onLoginRequested: page.requestCookies(true)
        onSignInRequired: if (page.restoringSession) page.showLogin()
        onLoadFailed: {
            if (page.restoringSession) {
                page.message = statusCode > 0
                        ? qsTr("Could not load the server (HTTP %1).").arg(statusCode)
                        : qsTr("Could not load the server. Check your connection.")
                page.showLogin()
            }
        }
    }
    WebView {
        id: loginWebView
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        opacity: page.restoringSession ? 0 : 1
        enabled: !page.restoringSession
        url: overleafClient.server + "/project"
        TouchInput {}
        KeyboardInput { enabled: true }
    }
    Column {
        anchors.centerIn: parent
        spacing: Theme.paddingMedium
        visible: page.restoringSession
        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            size: BusyIndicatorSize.Large
            running: page.restoringSession
        }
        Label {
            text: overleafClient.busy ? qsTr("Loading projects…") : qsTr("Connecting…")
            color: Theme.secondaryColor
        }
    }
    Connections {
        target: CookieManager
        onUrlCookiesAvailable: {
            if (!page.cookieRequestPending)
                return
            page.cookieRequestPending = false
            overleafClient.setInteractiveCookiePending(false)
            cookieTimeout.stop()
            if (page.cookieRequestAutomatic && !overleafClient.isProjectListUrl(loginWebView.url))
                return
            var result = []
            for (var i = 0; i < cookies.length; i++)
                result.push({ "name": cookies[i].name, "value": cookies[i].value })
            page.message = ""
            overleafClient.finishBrowserLogin(result)
        }
    }
    Connections {
        target: overleafClient
        onBackgroundCookiePendingChanged: deferredCookies.restart()
        onInteractiveCookiePendingChanged: deferredCookies.restart()
        onServerRemovalPendingChanged: deferredCookies.restart()
        onAuthenticatedChanged: {
            if (overleafClient.authenticated) {
                restoreTimeout.stop()
                pageStack.replace(Qt.resolvedUrl("ProjectsPage.qml"))
            }
        }
        onErrorChanged: if (page.restoringSession && overleafClient.error.length) page.showLogin()
    }
    Timer {
        id: deferredCookies
        interval: 1
        onTriggered: {
            if (page.automaticCookieDeferred && !overleafClient.backgroundCookiePending
                    && !overleafClient.interactiveCookiePending
                    && !overleafClient.serverRemovalPending
                    && overleafClient.isProjectListUrl(loginWebView.url))
                page.requestCookies(true)
        }
    }
}
