import QtQuick 2.0
import ru.auroraos.WebView 1.0

Item {
    Connections {
        target: CookieManager
        onUrlCookiesAvailable: {
            if (overleafClient.interactiveCookiePending)
                overleafClient.setInteractiveCookiePending(false)
        }
    }
}
