#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QFile>
#include <functional>
#include "window_chrome.h"
#include "text_focus.h"
#include <QScreen>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#endif

// Exercise the real QML with Qt's event delivery. The test has its own settings
// directory and never touches the user's preview preferences or real presets.
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ClumsierTests");
    app.setApplicationName("QtShell");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"settingsLocation", QUrl::fromLocalFile(settings.path() + "/preview.ini")}});
    bool warnings = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     [&](const QList<QQmlError> &) { warnings = true; });
    engine.load(QUrl::fromLocalFile(QStringLiteral(BETA_QML_DIR "/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) return 2;
    WindowChrome chrome(window);
    TextFocus textFocus(window);
    if (!window || !QTest::qWaitForWindowExposed(window)) return 2;
    QTest::qWait(250);
    auto check = [](bool ok, const char *description) {
        if (!ok) qCritical("FAIL: %s", description);
        return ok;
    };
    std::function<QQuickItem *(QQuickItem *, const QString &)> findItem;
    findItem = [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
        if (parent->objectName() == name) return parent;
        for (auto *child : parent->childItems())
            if (auto *found = findItem(child, name)) return found;
        return nullptr;
    };
    auto click = [&](const char *name) {
        if (QString::fromLatin1(name) == "themeButton") {
            // Every theme-button click in this test opens the picker. Wait for
            // the previous close animation, which can take extra frames when
            // the test window is occluded, before sending another click.
            if (!QTest::qWaitFor([&] { return !window->property("themePickerOpen").toBool(); }, 2000)) return false;
        }
        QQuickItem *item = nullptr;
        // Filtered ListView delegates are created on a later frame. A clean
        // package build can leave the desktop busier than an incremental run.
        if (!QTest::qWaitFor([&] { item = findItem(window->contentItem(), QString::fromLatin1(name)); return item != nullptr; }, 2500)) {
            qCritical("Missing click target: %s", name);
            return false;
        }
        // Settings can extend below the viewport at larger interface scales.
        // Scroll the owning Flickable before clicking its actual screen position.
        for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->property("contentY").isValid()) continue;
            const auto bounds = item->mapRectToItem(parent, item->boundingRect());
            const qreal delta = bounds.bottom() > parent->height() ? bounds.bottom() - parent->height() + 8
                              : bounds.top() < 0 ? bounds.top() - 8 : 0;
            if (delta) { parent->setProperty("contentY", parent->property("contentY").toReal() + delta); QTest::qWait(100); }
        }
        qInfo() << "Click" << name << item->mapToScene(QPointF(item->width()/2, item->height()/2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                         item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
        QTest::qWait(200);
        return true;
    };
    bool ok = click("startButton");
#ifdef Q_OS_WIN
    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DEFAULT;
    const HRESULT cornerResult = DwmGetWindowAttribute(reinterpret_cast<HWND>(window->winId()),
        DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    // This attribute is available on Windows 11; older systems keep their frame.
    if (SUCCEEDED(cornerResult))
        ok &= check(corners == DWMWCP_ROUND, "Native window requests rounded corners");
#endif
    auto *hoverStep = findItem(window->contentItem(), "step1");
    auto *hoverBackground = hoverStep ? hoverStep->property("background").value<QObject *>() : nullptr;
    ok &= check(hoverBackground != nullptr, "Step has a themed background");
    if (hoverBackground) {
        QTest::mouseMove(window, QPoint(500, 180));
        QTest::qWait(50);
        const QVariant idleColor = hoverBackground->property("color");
        const QPoint point = hoverStep->mapToScene(QPointF(60, hoverStep->height()/2)).toPoint();
        QTest::mouseMove(window, point); QTest::qWait(50);
        ok &= check(hoverStep->property("pointerInside").toBool() && hoverBackground->property("color") != idleColor,
                    "Unselected step visibly highlights on hover");
        QTest::mouseMove(window, QPoint(500, 180)); QTest::qWait(50);
        ok &= check(!hoverStep->property("pointerInside").toBool() && hoverBackground->property("color") == idleColor,
                    "Hover highlight clears on leave");
        click("step1"); click("step0");
        QTest::mouseMove(window, QPoint(500, 180)); QTest::qWait(50);
        ok &= check(!hoverStep->property("pointerInside").toBool() && hoverBackground->property("color") == idleColor,
                    "Previous selection does not retain a hover fill after clicking elsewhere");
    }
    ok &= check(window->property("running").toBool(), "Start click updates preview");
    QTest::keyClick(window, Qt::Key_F7);
    ok &= check(!window->property("running").toBool(), "F7 stops preview");
    ok &= click("step2");
    ok &= check(window->property("selectedStep").toInt() == 2, "Step selection");
    ok &= click("lowestMode");
    auto *target = findItem(window->contentItem(), "targetField");
    ok &= check(target && !target->isEnabled() && target->property("text").toString().isEmpty(),
                "Lowest available clears and disables the numeric target");
    ok &= click("targetMode");
    ok &= check(target->isEnabled() && target->property("text").toString() == "0",
                "Returning to target mode does not resurrect the old value");
    ok &= click("sidebarToggle");
    ok &= check(!window->property("sidebarOpen").toBool(), "Compact drawer closes");
    for (int attempt = 0; attempt < 3; ++attempt) {
        QTest::mouseMove(window, QPoint(500, 200));
        QTest::qWait(400);
        // Header, middle and footer must all reveal the detached compact panel.
        const int edgeY[] = {20, 350, 760};
        QTest::mouseMove(window, QPoint(2, edgeY[attempt]));
        QTest::qWait(50);
        ok &= check(window->property("sidebarOpen").toBool(), "Left edge reveals mouse peek");
        QTest::mouseMove(window, QPoint(8, edgeY[attempt]));
        QTest::qWait(250);
        ok &= check(window->property("sidebarOpen").toBool(), "Inset gap keeps panel open");
        auto *panel = findItem(window->contentItem(), "sequenceSidebar");
        ok &= check(panel && panel->x() > 0 && panel->y() > 0, "Compact panel is inset from the window edges");
        if (attempt == 0 && !qEnvironmentVariable("CLUMSIER_SCREENSHOTS").isEmpty()) {
            QDir().mkpath(qEnvironmentVariable("CLUMSIER_SCREENSHOTS"));
            window->grabWindow().save(qEnvironmentVariable("CLUMSIER_SCREENSHOTS") + "/compact.png");
        }
        ok &= click("newSequenceButton");
        QTest::mouseMove(window, QPoint(500, 200));
        QTest::qWait(450);
        ok &= check(!window->property("sidebarOpen").toBool(), "Mouse leave closes even after clicking inside");
    }
    QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
    QTest::qWait(200);
    ok &= check(window->property("sidebarOpen").toBool(), "Keyboard reveals drawer");
    QTest::mouseMove(window, QPoint(500, 200));
    QTest::qWait(450);
    ok &= check(window->property("sidebarOpen").toBool(), "Keyboard-open drawer stays available");
    ok &= click("sidebarToggle");
    QTest::mouseMove(window, QPoint(700, 80));
    QTest::qWait(400);
    ok &= click("tab-quick controls");
    ok &= check(window->property("page").toString() == "quick controls", "Tab navigation");
    ok &= click("settingsButton");
    ok &= click("scale-130");
    window->resize(760, 600);
    QTest::qWait(250);
    auto *settingsButton = findItem(window->contentItem(), "settingsButton");
    ok &= check(settingsButton && settingsButton->mapToScene(QPointF(settingsButton->width(), 0)).x() <= window->width(),
                "Navigation stays inside a narrow window at 130 percent");
    window->resize(1180, 1000);
    QTest::qWait(250);
    ok &= click("scale-100");
    ok &= check(window->property("interfaceScale").toDouble() == 1.0, "Scale returns to 100 percent");
    ok &= click("tab-sequences");
    auto typeText = [&](const QString &text) {
        // The opening animation finishes by clearing and focusing search.
        // Wait for that handoff instead of racing it with synthetic typing.
        ok &= check(QTest::qWaitFor([&] {
            auto *search = findItem(window->contentItem(), "themeSearch");
            return search && search->hasActiveFocus();
        }, 1200), "Theme search is ready for typing");
        for (auto c : text) QTest::keyClick(window, static_cast<Qt::Key>(c.toUpper().unicode()));
        QTest::qWait(100);
    };
    ok &= click("themeButton");
    ok &= check(window->property("themePickerOpen").toBool(), "Theme picker opens");
    typeText("paper");
    ok &= check(window->property("activeTheme").toString() == "paper", "Search previews a light theme");
    ok &= check(window->property("savedTheme").toString() == "lavender", "Preview does not save");
    QTest::keyClick(window, Qt::Key_F7);
    ok &= check(!window->property("running").toBool(), "Picker suppresses playback shortcuts");
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(200);
    ok &= check(window->property("activeTheme").toString() == "lavender", "Escape restores saved theme");
    ok &= click("themeButton");
    typeText("paper");
    ok &= click("favorite-paper");
    ok &= check(window->property("themePickerOpen").toBool(), "Favoriting does not close picker");
    QTest::keyClick(window, Qt::Key_Return);
    QTest::qWait(250);
    ok &= check(window->property("savedTheme").toString() == "paper", "Enter commits theme");
    QSettings savedSettings(settings.path() + "/preview.ini", QSettings::IniFormat);
    ok &= check(savedSettings.value("shell/themeId").toString() == "paper", "Chosen theme persisted");
    ok &= check(savedSettings.value("shell/favoriteThemeIds").toString().contains("paper"), "Favorite persisted");
    ok &= click("themeButton");
    typeText("catppuccin");
    QTest::keyClick(window, Qt::Key_Down);
    QTest::qWait(100);
    ok &= check(window->property("activeTheme").toString() == "tinted-catppuccin-latte", "Arrow key previews light bundled palette");
    QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Down);
    QTest::qWait(100);
    ok &= check(window->property("activeTheme").toString() == "tinted-catppuccin-mocha", "Arrow key previews bundled palette");
    QTest::keyClick(window, Qt::Key_Up);
    QTest::qWait(100);
    ok &= check(window->property("activeTheme").toString() == "tinted-catppuccin-macchiato", "Arrow key moves to previous bundled palette");
    auto *hoverRow = findItem(window->contentItem(), "theme-row-tinted-catppuccin-mocha");
    ok &= check(hoverRow != nullptr, "Hover preview row exists");
    if (hoverRow) {
        const QPoint rowPoint = hoverRow->mapToScene(QPointF(80, hoverRow->height()/2)).toPoint();
        QTest::mouseMove(window, rowPoint);
        QTest::qWait(250);
        ok &= check(window->property("activeTheme").toString() == "tinted-catppuccin-macchiato", "Brief hover does not preview");
        QTest::mouseMove(window, rowPoint + QPoint(2,0));
        // Allow event delivery/rendering to catch up on a busy desktop. The
        // preceding assertion still checks that a brief hover cannot preview.
        ok &= check(QTest::qWaitFor([&] { return window->property("activeTheme").toString() == "tinted-catppuccin-mocha"; }, 1200),
                    "Continuous hover previews after the dwell delay");
        QTest::keyClick(window, Qt::Key_Up);
        QTest::mouseMove(window, QPoint(30,300));
        QTest::mouseMove(window, rowPoint);
        QTest::qWait(100);
        QTest::mouseMove(window, QPoint(30,300));
        QTest::qWait(550);
        ok &= check(window->property("activeTheme").toString() == "tinted-catppuccin-macchiato", "Leaving row cancels pending preview");
    }
    QTest::qWait(260);
    if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS"))
        window->grabWindow().save(qEnvironmentVariable("CLUMSIER_SCREENSHOTS") + "/theme-picker.png");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(30,300));
    QTest::qWait(200);
    ok &= check(!window->property("themePickerOpen").toBool() && window->property("activeTheme").toString() == "paper", "Outside click cancels preview");
    ok &= click("themeButton");
    typeText("no matching theme");
    QTest::keyClick(window, Qt::Key_Return);
    ok &= check(window->property("themePickerOpen").toBool(), "Empty search cannot apply a theme");
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(200);
    ok &= click("themeButton");
    typeText("lavender");
    QTest::keyClick(window, Qt::Key_Return);
    QTest::qWait(250);
    ok &= check(window->flags().testFlag(Qt::FramelessWindowHint), "Native title bar is removed");
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(110, 25));
    QTest::qWait(100);
    ok &= check(window->visibility() == QWindow::Windowed, "Double-clicking header does not maximize");
    ok &= click("maximizeButton");
    QTest::qWait(400);
#ifdef Q_OS_WIN
    // On desktops with no reserved taskbar area Qt labels a monitor-sized
    // client FullScreen. Windows' native maximized flag is authoritative here.
    ok &= check(QTest::qWaitFor([&] { return IsZoomed(reinterpret_cast<HWND>(window->winId())); }, 5000), "Windows reports a maximized window");
#else
    ok &= check(window->visibility() == QWindow::Maximized, "Header maximizes window");
#endif
    ok &= check(window->screen()->availableGeometry().contains(window->geometry()), "Maximize respects the available work area");
    ok &= click("maximizeButton");
    ok &= check(QTest::qWaitFor([&] { return window->visibility() == QWindow::Windowed; }, 5000), "Header restores window");
    ok &= click("minimizeButton");
    ok &= check(window->visibility() == QWindow::Minimized, "Header minimizes window");
    window->showNormal();
    QTest::qWait(250);

    // Save real renders for review at both the default and narrow window sizes.
    const QString output = qEnvironmentVariable("CLUMSIER_SCREENSHOTS");
    if (!output.isEmpty()) QDir().mkpath(output);
    for (const auto size : {QSize(1180, 780), QSize(760, 600)}) {
        window->resize(size);
        QTest::qWait(250);
        const auto rendered = window->grabWindow();
        ok &= check(!rendered.isNull(), "Qt renders a nonempty window");
        if (!output.isEmpty())
            ok &= rendered.save(output + "/shell-" + QString::number(size.width()) + ".png");
    }
    ok &= check(!warnings, "No QML warnings during interactions");
    ok &= click("closeButton");
    ok &= check(!window->isVisible(), "Header closes window");
    qInfo("Qt shell checks %s", ok ? "passed" : "failed");
    return ok ? 0 : 3;
}
