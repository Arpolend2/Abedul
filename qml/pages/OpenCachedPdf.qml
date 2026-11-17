import QtQuick 2.0
import Sailfish.Silica 1.0

QtObject {
    property var client: overleafClient
    property string error: ""

    function open() {
        error = ""
        var url = client.cachedPdfForExternalOpen()
        if (!url.toString().length) {
            error = client.pdfError
            return
        }
        URIOpener.openUri(url, function() {}, function(message) {
            error = qsTr("Could not open the PDF: %1").arg(message)
        })
    }
}
