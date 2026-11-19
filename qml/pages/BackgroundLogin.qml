import QtQuick 2.0

Item {
    id: controller
    property var client

    function attempt() {
        if (client && client.offline && !client.busy && !client.interactiveCookiePending
                && !client.serverRemovalPending)
            client.resumeSavedSession()
    }

    Component.onCompleted: if (client && client.offline) kick.restart()

    Timer {
        id: retry
        interval: 30000
        onTriggered: controller.attempt()
    }
    Timer {
        id: kick
        interval: 1
        onTriggered: controller.attempt()
    }
    Connections {
        target: controller.client
        onOfflineChanged: {
            if (controller.client.offline)
                kick.restart()
            else
                retry.stop()
        }
        onBusyChanged: {
            if (controller.client.offline && !controller.client.busy)
                retry.restart()
        }
        onInteractiveCookiePendingChanged: {
            if (controller.client.offline && !controller.client.interactiveCookiePending)
                kick.restart()
        }
        onServerRemovalPendingChanged: {
            if (controller.client.offline && !controller.client.serverRemovalPending)
                kick.restart()
        }
        onReconnectRequested: {
            retry.stop()
            controller.attempt()
        }
    }
}
