import QtQuick 2.0

Item {
    id: controller
    property var webView
    property var client
    property bool attempted: false
    property bool loadSucceeded: false
    signal loginRequested()
    signal signInRequired()
    signal loadFailed(int statusCode)

    function updateNavigation() {
        if (!client || !webView)
            return
        if (!client.isProjectListUrl(webView.url)) {
            delay.stop()
            attempted = false
        }
        if (!webView.loading && loadSucceeded) {
            delay.restart()
        }
    }

    Timer {
        id: delay
        interval: 250
        onTriggered: {
            if (controller.client && controller.webView
                    && !controller.webView.loading && controller.loadSucceeded) {
                if (controller.client.isProjectListUrl(controller.webView.url)) {
                    if (!controller.attempted) {
                        controller.attempted = true
                        controller.loginRequested()
                    }
                } else {
                    controller.signInRequired()
                }
            }
        }
    }

    Connections {
        target: controller.webView
        onUrlChanged: controller.updateNavigation()
        onLoadStart: {
            delay.stop()
            controller.loadSucceeded = false
        }
        onLoadingChanged: controller.updateNavigation()
        onLoadFinished: {
            controller.loadSucceeded = httpStatusCode >= 200 && httpStatusCode < 300
            if (httpStatusCode < 200 || httpStatusCode >= 400)
                controller.loadFailed(httpStatusCode)
            controller.updateNavigation()
        }
    }
}
