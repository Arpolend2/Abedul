#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QtQml>
#include <QScopedPointer>
#include <auroraapp.h>
#include <webenginecontext.h>

#include "overleafclient.h"
#include "draftstore.h"
#include "latexhighlighter.h"

int main(int argc, char **argv)
{
    QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QScopedPointer<QGuiApplication> application(Aurora::Application::application(argc, argv));
    application->setOrganizationName(QStringLiteral("ru.auroraos"));
    application->setApplicationName(QStringLiteral("OverleafClient"));
    application->setApplicationDisplayName(QStringLiteral("Abedul"));
    qmlRegisterType<LatexHighlighter>("Abedul", 1, 0, "LatexHighlighter");

    OverleafClient client;
    DraftStore drafts;
    QScopedPointer<QQuickView> view(Aurora::Application::createView());
    Aurora::WebView::WebEngineContext::InitBrowser(argc, argv, {"--default-encoding=UTF-8"});
    view->rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    view->rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    view->setSource(Aurora::Application::pathTo(QStringLiteral("qml/OverleafClient.qml")));
    view->show();
    return application->exec();
}
