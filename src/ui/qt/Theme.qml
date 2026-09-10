import QtQuick
import "ThemeCatalog.js" as Catalog

Item {
    id: theme
    property bool ready: false
    Component.onCompleted: ready = true
    property string activeId: "lavender"
    readonly property var colors: Catalog.find(activeId)
    property color background: colors.background
    property color surface: colors.surface
    property color text: colors.text
    readonly property color targetText: colors.text
    readonly property color targetBackground: colors.background
    // Syntax-comment colors can be hard to read as UI labels. Keep secondary
    // text closer to the foreground while preserving the palette's hue.
    property color muted: colors.muted || Qt.tint(targetBackground, Qt.rgba(targetText.r, targetText.g, targetText.b, 0.68))
    property color accent: colors.accent
    property color good: colors.good
    // Derive interaction surfaces from the palette so light themes work too.
    readonly property color hover: Qt.tint(background, Qt.rgba(text.r, text.g, text.b, 0.07))
    readonly property color selected: Qt.tint(background, Qt.rgba(accent.r, accent.g, accent.b, 0.13))
    readonly property color border: Qt.tint(background, Qt.rgba(text.r, text.g, text.b, 0.20))
    readonly property string family: Qt.platform.os === "windows" ? "Consolas" : "monospace"
    property bool reducedMotion: false
    property real scale: 1
    readonly property int motion: reducedMotion ? 0 : 150

    // Animate shared palette values once; every control follows the same fade.
    // Derived hover and border colors follow those values without extra timers.
    Behavior on background { enabled: theme.ready && !theme.reducedMotion; ColorAnimation { duration: 240 } }
    Behavior on surface { enabled: theme.ready && !theme.reducedMotion; ColorAnimation { duration: 240 } }
    Behavior on text { enabled: theme.ready && !theme.reducedMotion; ColorAnimation { duration: 240 } }
    Behavior on muted { enabled: theme.ready && !theme.reducedMotion; ColorAnimation { duration: 240 } }
    Behavior on accent { enabled: theme.ready && !theme.reducedMotion; ColorAnimation { duration: 240 } }
    Behavior on good { enabled: theme.ready && !theme.reducedMotion; ColorAnimation { duration: 240 } }
}
