#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QtQml>
#include <QScopedPointer>
#include <QLocale>
#include <QTranslator>
#include <auroraapp.h>
#include <webenginecontext.h>

#include "overleafclient.h"
#include "draftstore.h"
#include "latexhighlighter.h"
#include "pdfgeometry.h"
#include "doubletapmonitor.h"
#include "completionkeyfilter.h"
#include "openrouterclient.h"

int main(int argc, char **argv)
{
    QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QScopedPointer<QGuiApplication> application(Aurora::Application::application(argc, argv));
    application->setOrganizationName(QStringLiteral("ru.arpolend"));
    application->setApplicationName(QStringLiteral("abedul"));
    application->setApplicationDisplayName(QStringLiteral("Abedul"));
    application->setApplicationVersion(QStringLiteral(ABEDUL_VERSION));
    QTranslator translator;
    const QLocale locale = QLocale::system();
    if (locale.language() == QLocale::Russian) {
        const QString translations = Aurora::Application::pathTo(QStringLiteral("translations")).toLocalFile();
        if (translator.load(locale, QStringLiteral("Abedul"), QStringLiteral("_"), translations))
            application->installTranslator(&translator);
        else
            qWarning("Could not load Russian application translations");
    }
    qmlRegisterType<LatexHighlighter>("Abedul", 1, 0, "LatexHighlighter");
    qmlRegisterType<PdfGeometry>("Abedul", 1, 0, "PdfGeometry");
    qmlRegisterType<DoubleTapMonitor>("Abedul", 1, 0, "DoubleTapMonitor");
    qmlRegisterType<CompletionKeyFilter>("Abedul", 1, 0, "CompletionKeyFilter");
    qmlRegisterType<OpenRouterClient>("Abedul", 1, 0, "OpenRouterClient");

    OverleafClient client;
    DraftStore drafts;
    drafts.setAccount(QString(), QString());
    QObject::connect(&client, &OverleafClient::accountChanged, &drafts, [&]() {
        drafts.setAccount(client.server(), client.userId());
    });
    QScopedPointer<QQuickView> view(Aurora::Application::createView());
    view->engine()->addImportPath(Aurora::Application::pathTo(QString()).toLocalFile());
    Aurora::WebView::WebEngineContext::InitBrowser(argc, argv, {"--default-encoding=UTF-8"});
    view->rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    view->rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"), application->applicationVersion());
    view->setSource(Aurora::Application::pathTo(QStringLiteral("qml/OverleafClient.qml")));
    view->show();
    return application->exec();
}
