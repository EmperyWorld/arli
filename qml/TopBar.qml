pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Rectangle {
    id: root
    required property var desk
    property bool sidebarShown: false
    property bool inspectorShown: false
    signal toggleSidebar()
    signal toggleInspector()
    signal commands()
    property string layoutMode: "full"
    signal chooseLayout(string mode)
    implicitHeight: Theme.dense ? 50 : 60
    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border
    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        Button {
            objectName: "sidebarButton"
            text: "☰"; implicitWidth: 38; flat: true
            Accessible.name: "Диалоги"
            ToolTip.visible: hovered; ToolTip.text: "Диалоги · Ctrl+F"
            onClicked: root.toggleSidebar()
        }
        Text { text: "Arli  /"; color: Theme.gold; font.pixelSize: Theme.fontSize(20); font.bold: true }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.border }
        Repeater {
            model: ["Чат", "Настройки", "Об Арли"]
            delegate: Button {
                required property string modelData
                required property int index
                objectName: "pageButton" + index
                text: modelData
                flat: true; checked: root.desk.page === index
                onClicked: root.desk.page = index
            }
        }
        Item { Layout.fillWidth: true }
        RowLayout {
            visible: root.width > 1180 && root.desk.page === 0
            spacing: 2
            Repeater {
                model: [{mode: "full", label: "Стол"}, {mode: "focus", label: "Фокус"}, {mode: "review", label: "Разбор"}]
                delegate: Button {
                    required property var modelData
                    objectName: "layoutButton_" + modelData.mode
                    text: modelData.label; flat: true; checked: root.layoutMode === modelData.mode
                    Accessible.name: "Компоновка: " + modelData.label
                    onClicked: root.chooseLayout(modelData.mode)
                }
            }
        }
        StatusLamp { phase: root.desk.daemonUp ? root.desk.workerState : "offline" }
        Text {
            visible: root.width > 1000
            text: root.desk.daemonUp ? (root.desk.busy ? "Выполняет задачу" : "На связи") : "Нет связи"
            color: root.desk.daemonUp ? Theme.muted : Theme.crimson
            font.pixelSize: Theme.fontSize(12)
        }
        Button {
            objectName: "stepsButton"
            text: "Действия"; flat: true; checked: root.inspectorShown
            visible: root.desk.page === 0
            ToolTip.visible: hovered; ToolTip.text: "Действия и результаты инструментов"
            onClicked: root.toggleInspector()
        }
        Button {
            text: "⌘"; implicitWidth: 38; flat: true
            Accessible.name: "Палитра команд"
            ToolTip.visible: hovered; ToolTip.text: "Команды · Ctrl+K"
            onClicked: root.commands()
        }
    }
}
