// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <gmock/gmock.h>

#include "global/runtime.h"
#include "testing/environment.h"

int main(int argc, char** argv)
{
    // Keep this executable usable directly as well as through CTest.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#ifdef Q_OS_WIN
    const QString windowsFonts = QStringLiteral("C:/Windows/Fonts");
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && QDir(windowsFonts).exists()) {
        qputenv("QT_QPA_FONTDIR", windowsFonts.toUtf8());
    }
#endif
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    // Offscreen's default font may have no Japanese glyphs. Explicitly load
    // Windows' installed font so saved dialog captures retain readable labels.
    const int fontId = QFontDatabase::addApplicationFont(windowsFonts + QStringLiteral("/meiryo.ttc"));
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    if (!families.isEmpty()) {
        application.setFont(QFont(families.front(), 10));
    }
#endif
    qputenv("QML_DISABLE_DISK_CACHE", "true");
    muse::runtime::mainThreadId();
    muse::runtime::setThreadName("main");
    muse::testing::Environment::setup();
    testing::InitGoogleMock(&argc, argv);
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const int result = RUN_ALL_TESTS();
    muse::testing::Environment::deinit();
    return result;
}
