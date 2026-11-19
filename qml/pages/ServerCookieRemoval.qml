import QtQuick 2.0

Item {
    id: controller
    property var client
    property var cookieManager
    property string server: ""
    property var pendingCookies: []
    property string phase: ""
    property bool timedOut: false

    function fail(message) {
        timeout.stop()
        phase = ""
        pendingCookies = []
        client.setInteractiveCookiePending(false)
        client.setServerRemovalPending(false)
        client.reportAccountRemovalError(message)
    }

    function start(address) {
        if (phase.length || client.interactiveCookiePending)
            return
        server = address
        phase = "reading"
        timedOut = false
        client.setInteractiveCookiePending(true)
        if (!cookieManager.retrieveAllCookies()) {
            fail(qsTr("Cannot access browser cookies. Open the sign-in page and try again."))
            return
        }
        timeout.restart()
    }

    function removeNext() {
        if (!pendingCookies.length) {
            phase = "verifying"
            if (!cookieManager.retrieveAllCookies()) {
                fail(qsTr("Could not verify browser cookie removal. The server was not removed."))
                return
            }
            timeout.restart()
            return
        }
        phase = "deleting"
        var cookie = pendingCookies.shift()
        if (!cookieManager.deleteCookies(cookie.url, cookie.name)) {
            fail(qsTr("Could not remove browser cookies. The server was not removed."))
            return
        }
        timeout.restart()
    }

    Timer {
        id: timeout
        interval: 10000
        onTriggered: {
            controller.timedOut = true
            controller.client.reportAccountRemovalError(qsTr("Removing browser cookies timed out. The server was not removed."))
        }
    }
    Connections {
        target: controller.cookieManager
        onAllCookiesAvailable: {
            if (controller.phase !== "reading" && controller.phase !== "verifying")
                return
            if (controller.timedOut) {
                controller.fail(qsTr("Removing browser cookies timed out. The server was not removed."))
                return
            }
            timeout.stop()
            var pending = []
            var seen = {}
            for (var i = 0; i < cookies.length; ++i) {
                var cookie = cookies[i]
                var url = controller.client.cookieRemovalUrl(controller.server, cookie.domain, cookie.path)
                if (!url.length) {
                    if (controller.client.cookieRemovalUrl(controller.server, cookie.domain, "/").length) {
                        controller.fail(qsTr("Could not remove browser cookies. The server was not removed."))
                        return
                    }
                    continue
                }
                if (!cookie.name) {
                    controller.fail(qsTr("Could not remove browser cookies. The server was not removed."))
                    return
                }
                if (controller.phase === "verifying") {
                    controller.fail(qsTr("Some browser cookies could not be removed. The server was not removed."))
                    return
                }
                var key = url + "\n" + cookie.name
                if (seen[key])
                    continue
                seen[key] = true
                pending.push({ "url": url, "name": cookie.name })
            }
            if (controller.phase === "verifying") {
                controller.phase = ""
                controller.client.setInteractiveCookiePending(false)
                controller.client.removeSavedServer(controller.server)
                controller.client.setServerRemovalPending(false)
                return
            }
            controller.pendingCookies = pending
            controller.removeNext()
        }
        onCookiesDeleted: {
            if (controller.phase !== "deleting")
                return
            if (controller.timedOut) {
                controller.fail(qsTr("Removing browser cookies timed out. The server was not removed."))
                return
            }
            timeout.stop()
            controller.removeNext()
        }
    }
}
