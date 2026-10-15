pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

ColumnLayout {
    id: settingsRoot
    required property var desk
    spacing: 12
    property int section: 0
    Shortcut { sequence: "Ctrl+S"; enabled: settingsRoot.visible && settingsRoot.desk.settingsDirty; onActivated: settingsRoot.desk.saveSettings() }

    Text { text: "Настройки рабочего места"; color: Theme.ink; font.pixelSize: Theme.fontSize(23); font.bold: true }
    Panel {
        Layout.fillWidth: true
        implicitHeight: saveRow.implicitHeight + 28
        ColumnLayout {
            id: saveRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 8
            Button {
                text: settingsRoot.desk.settingsDirty ? "Сохранить настройки" : "Настройки сохранены"
                highlighted: settingsRoot.desk.settingsDirty
                enabled: settingsRoot.desk.settingsDirty && settingsRoot.desk.daemonUp
                onClicked: settingsRoot.desk.saveSettings()
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: settingsRoot.desk.settingsDirty ? Theme.amber : Theme.emerald
                font.pixelSize: Theme.fontSize(13)
                text: settingsRoot.desk.settingsDirty ? "Есть несохранённые изменения · Ctrl+S. Применятся со следующего хода." : settingsRoot.desk.settingsState
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Repeater {
            model: ["Модель", "Файлы", "Разрешения", "Выполнение", "Интерфейс"]
            delegate: Button {
                required property string modelData
                required property int index
                objectName: "settingsSection" + index
                Layout.fillWidth: true
                ToolTip.visible: hovered; ToolTip.text: modelData
                text: modelData; flat: true; checked: settingsRoot.section === index
                onClicked: { settingsRoot.section = index; settingsScroll.contentY = 0 }
            }
        }
        Item { Layout.fillWidth: true }
    }
    Flickable {
        id: settingsScroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: width
        contentHeight: grid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        GridLayout {
            id: grid
            width: parent.width
            columns: 1
            columnSpacing: 16
            rowSpacing: 16

        AppearanceSettings { Layout.fillWidth: true; visible: settingsRoot.section === 4 }
        ModelSettings { Layout.fillWidth: true; visible: settingsRoot.section === 0; desk: settingsRoot.desk }

        FolderSettings { Layout.fillWidth: true; visible: settingsRoot.section === 1; desk: settingsRoot.desk }

        PermissionSettings { Layout.fillWidth: true; visible: settingsRoot.section === 2; desk: settingsRoot.desk }

        ContextSettings { Layout.fillWidth: true; visible: settingsRoot.section === 3; desk: settingsRoot.desk }

        TimeoutSettings { Layout.fillWidth: true; visible: settingsRoot.section === 3; desk: settingsRoot.desk }

        ConnectionSettings { Layout.fillWidth: true; visible: settingsRoot.section === 3; desk: settingsRoot.desk }
        }
    }
}
