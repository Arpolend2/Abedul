import QtQuick 2.0
import ru.auroraos.WebView 1.0

Item {
    id: service
    width: 1
    height: 1
    opacity: 0
    property string requestedServer: ""
    property bool browserReady: false

    function start(address) {
        requestedServer = address
        if (browserReady)
            beginRemoval()
        else
            startupTimeout.restart()
    }

    function beginRemoval() {
        startupTimeout.stop()
        if (controller.active)
            controller.item.start(requestedServer)
        else
            controller.active = true
    }

    WebView {
        width: 1
        height: 1
        url: "about:blank"
        onLoadFinished: {
            service.browserReady = true
            if (service.requestedServer.length && overleafClient.serverRemovalPending)
                service.beginRemoval()
        }
    }
    Loader {
        id: controller
        active: false
        sourceComponent: Component {
            ServerCookieRemoval {
                client: overleafClient
                cookieManager: CookieManager
            }
        }
        onLoaded: item.start(service.requestedServer)
        onStatusChanged: {
            if (status === Loader.Error) {
                overleafClient.setServerRemovalPending(false)
                overleafClient.reportAccountRemovalError(qsTr("Could not load browser cookie removal."))
            }
        }
    }
    Timer {
        id: startupTimeout
        interval: 15000
        onTriggered: {
            overleafClient.setServerRemovalPending(false)
            overleafClient.reportAccountRemovalError(qsTr("Could not start the browser to remove cookies."))
        }
    }
}
