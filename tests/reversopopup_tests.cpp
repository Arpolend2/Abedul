#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtQml>

#include "reversoclient.h"

static QQuickItem *findVisualChild(QQuickItem *parent, const QString &name)
{
    for (QQuickItem *child : parent->childItems()) {
        if (child->objectName() == name)
            return child;
        if (QQuickItem *result = findVisualChild(child, name))
            return result;
    }
    return nullptr;
}

class ReversoPopupTests : public QObject
{
    Q_OBJECT
private slots:
    void popupAndLanguages();
};

void ReversoPopupTests::popupAndLanguages()
{
    qmlRegisterType<ReversoClient>("Abedul", 1, 0, "ReversoClient");
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick 2.5\n"
        "Item { width: 600; height: 800; "
        "ReversoPopup { anchors.top: parent.top; anchors.right: parent.right } }",
        QUrl::fromLocalFile(QStringLiteral(REVERSO_QML_DIR "/ReversoPopupTest.qml")));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QQuickItem *parent = qobject_cast<QQuickItem *>(object.data());
    QQuickItem *popup = object->findChild<QQuickItem *>(QStringLiteral("reversoPopup"));
    QObject *source = object->findChild<QObject *>(QStringLiteral("reversoSourceLanguage"));
    QObject *target = object->findChild<QObject *>(QStringLiteral("reversoTargetLanguage"));
    QObject *input = object->findChild<QObject *>(QStringLiteral("reversoInput"));
    QObject *result = object->findChild<QObject *>(QStringLiteral("reversoResult"));
    QQuickItem *swap = object->findChild<QQuickItem *>(QStringLiteral("reversoSwapLanguages"));
    QQuickItem *inputContainer = object->findChild<QQuickItem *>(QStringLiteral("reversoInputContainer"));
    QQuickItem *submit = object->findChild<QQuickItem *>(QStringLiteral("reversoSubmit"));
    QVERIFY(parent && popup && source && target && input && result && swap && inputContainer && submit);
    QVERIFY(!object->findChild<QObject *>(QStringLiteral("reversoClose")));
    QVERIFY(!object->findChild<QObject *>(QStringLiteral("reversoQuickClose")));
    QCOMPARE(source->property("languageCode").toString(), QStringLiteral("ru"));
    QCOMPARE(target->property("languageCode").toString(), QStringLiteral("en"));
    QObject *menu = target->property("menu").value<QObject *>();
    QVERIFY(menu);
    QVERIFY(QMetaObject::invokeMethod(menu, "activated", Q_ARG(int, 2)));
    QCOMPARE(target->property("currentIndex").toInt(), 2);
    QCOMPARE(target->property("languageCode").toString(), QStringLiteral("de"));
    QObject *selected = target->property("currentItem").value<QObject *>();
    QVERIFY(selected);
    QCOMPARE(selected->property("text").toString(), QStringLiteral("German"));
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QVERIFY(popup->isVisible());
    QVERIFY(inputContainer->isVisible());
    QVERIFY(submit->isVisible());
    QVERIFY(QMetaObject::invokeMethod(popup, "swapLanguages"));
    QCOMPARE(source->property("languageCode").toString(), QStringLiteral("de"));
    QCOMPARE(target->property("languageCode").toString(), QStringLiteral("ru"));
    QVERIFY(QMetaObject::invokeMethod(popup, "swapLanguages"));
    QCOMPARE(source->property("languageCode").toString(), QStringLiteral("ru"));
    QCOMPARE(target->property("languageCode").toString(), QStringLiteral("de"));
    input->setProperty("text", QString());
    QVERIFY(QMetaObject::invokeMethod(popup, "submit"));
    QVERIFY(result->property("text").toString().contains(QStringLiteral("Enter text")));
    QVERIFY(QMetaObject::invokeMethod(popup, "translateText", Q_ARG(QVariant, QString())));
    QVERIFY(popup->property("reverseDirection").toBool());
    QVERIFY(!inputContainer->isVisible());
    QVERIFY(!submit->isVisible());
    const QString selectedText(1001, QLatin1Char('a'));
    QVERIFY(QMetaObject::invokeMethod(popup, "translateText", Q_ARG(QVariant, selectedText)));
    QCOMPARE(popup->property("translatedWord").toString(), selectedText);
    QVERIFY(result->property("text").toString().contains(QStringLiteral("too long")));
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QVERIFY(inputContainer->isVisible());
    QVERIFY(submit->isVisible());
    QVERIFY(!popup->property("reverseDirection").toBool());
    QVERIFY(!popup->property("submitted").toBool());
    for (const QSize &size : {QSize(360, 640), QSize(640, 360), QSize(600, 800), QSize(800, 360)}) {
        parent->setWidth(size.width());
        parent->setHeight(size.height());
        QCoreApplication::processEvents();
        QVERIFY(popup->width() <= parent->width());
        QVERIFY(popup->height() <= parent->height());
        QVERIFY(popup->x() >= 0);
        QVERIFY(popup->x() + popup->width() <= parent->width());
        QVERIFY(swap->x() >= 0);
        QQuickItem *targetPicker = qobject_cast<QQuickItem *>(target);
        QVERIFY(targetPicker);
        QVERIFY(targetPicker->x() + targetPicker->width() <= swap->parentItem()->width() + 1);
    }
    if (qEnvironmentVariableIsSet("ABEDUL_TEST_REVERSO_LIVE")) {
        ReversoClient *client = object->findChild<ReversoClient *>(QStringLiteral("editorReversoClient"));
        QVERIFY(client);
        source->setProperty("currentIndex", 1);
        target->setProperty("currentIndex", 0);
        input->setProperty("text", QStringLiteral("happy"));
        QVERIFY(QMetaObject::invokeMethod(popup, "submit"));
        QTRY_VERIFY_WITH_TIMEOUT(!client->busy(), 20000);
        QVERIFY2(client->error().isEmpty(), qPrintable(client->error()));
        QVERIFY(!client->translations().isEmpty());
        QVERIFY(!client->examples().isEmpty());
        QTRY_VERIFY(findVisualChild(popup, QStringLiteral("reversoTranslation")));
        QTRY_VERIFY(findVisualChild(popup, QStringLiteral("reversoExampleSource")));
        QVERIFY(findVisualChild(popup, QStringLiteral("reversoExampleSource"))->property("text").toString()
                    .contains(client->examples().first().toMap().value(QStringLiteral("source")).toString()));
        source->setProperty("currentIndex", 0);
        target->setProperty("currentIndex", 1);
        const QString sentence = QStringLiteral("I am happy to see you today");
        QVERIFY(QMetaObject::invokeMethod(popup, "translateText", Q_ARG(QVariant, sentence)));
        QCOMPARE(popup->property("translatedWord").toString(), sentence);
        QTRY_VERIFY_WITH_TIMEOUT(!client->busy(), 20000);
        QVERIFY2(client->error().isEmpty(), qPrintable(client->error()));
        QVERIFY(!client->translations().isEmpty());
        QVERIFY(!client->examples().isEmpty());
        QVERIFY(popup->property("reverseDirection").toBool());
        QTRY_VERIFY(findVisualChild(popup, QStringLiteral("reversoExampleTarget"))
                    && findVisualChild(popup, QStringLiteral("reversoExampleTarget"))->property("text").toString()
                           .contains(client->examples().first().toMap().value(QStringLiteral("target")).toString()));
    }
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QVERIFY(!popup->isVisible());
}

QTEST_MAIN(ReversoPopupTests)
#include "reversopopup_tests.moc"
