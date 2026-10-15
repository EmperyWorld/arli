import QtQuick
import QtQuick.Controls
import QtCore
import Arli

pragma ComponentBehavior: Bound

ApplicationWindow {
    id: root
    required property var shellAgent
    width: Math.min(1480, Screen.desktopAvailableWidth || 1480)
    height: Math.min(920, Screen.desktopAvailableHeight || 920)
    minimumWidth: 800
    minimumHeight: 600
    visibility: Window.Windowed
    Settings {
        category: "window"
        property alias width: root.width
        property alias height: root.height
        property alias x: root.x
        property alias y: root.y
    }
    Shortcut { sequence: "F11"; onActivated: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    visible: true
    title: root.shellAgent.dialogTitle.length ? ("Arli — " + root.shellAgent.dialogTitle) : "Arli"
    color: Theme.bg
    palette.window: Theme.bg
    palette.windowText: Theme.ink
    palette.button: Theme.elevated
    palette.buttonText: Theme.ink
    palette.base: Theme.bg
    palette.text: Theme.ink
    palette.highlight: Theme.gold
    palette.highlightedText: Theme.light ? "#ffffff" : "#1a1408"
    palette.mid: Theme.border
    palette.dark: Theme.gold
    palette.placeholderText: Theme.faint

    AgentPage {
        anchors.fill: parent
        desk: root.shellAgent
    }
}
