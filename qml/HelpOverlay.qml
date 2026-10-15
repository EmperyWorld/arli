import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Rectangle {
    id: overlayRoot
    required property var desk
    anchors.fill: parent
    visible: overlayRoot.desk.helpOpen
    color: "#d007060b"

    MouseArea { anchors.fill: parent; onClicked: overlayRoot.desk.helpOpen = false }

    Panel {
        width: Math.min(parent.width - 96, 680)
        height: Math.min(parent.height - 72, grid.implicitHeight + 48)
        anchors.centerIn: parent
        fill: Theme.panel
        border.color: Theme.goldDim

        Flickable {
            anchors.fill: parent
            anchors.margins: 20
            contentWidth: width
            contentHeight: grid.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: grid
            width: parent.width
            spacing: 14

            SectionHead { kicker: "КЛАВИАТУРА"; title: "Как вести стол" }

            GridLayout {
                Layout.fillWidth: true
                columns: width < 620 ? 1 : 2
                columnSpacing: 18
                rowSpacing: 10
                Repeater {
                    model: [
                        ["Ctrl+Enter", "отправить"],
                        ["Esc", "стоп или закрыть"],
                        ["Ctrl+N", "новый диалог"],
                        ["Ctrl+K", "палитра команд"],
                        ["Ctrl+\\", "полный / чтение / разбор"],
                        ["Ctrl+Tab", "следующий диалог"],
                        ["Ctrl+F", "фильтр диалогов"],
                        ["Ctrl+Shift+F", "поиск в переписке"],
                        ["/", "фильтр, если курсор не в тексте"],
                        ["Ctrl+.", "поле ввода, если курсор не в тексте"],
                        ["Ctrl+1", "чат"],
                        ["Ctrl+2", "настройки"],
                        ["Ctrl+3", "роль Арли"],
                        ["Ctrl+,", "переключить страницу"],
                        ["?", "подсказки вне поля ввода"],
                        ["Ctrl+S", "сохранить настройки"],
                        ["F11", "полный экран"]
                    ]
                    delegate: RowLayout {
                        id: shortcutRow
                        required property var modelData
                        Layout.fillWidth: true
                        Chip { label: shortcutRow.modelData[0]; ink: Theme.gold; wash: Theme.goldWash }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: shortcutRow.modelData[1]; color: Theme.ink; font.pixelSize: Theme.fontSize(14) }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: "Стоп просит воркер остановиться. Жёстко убивает его и поднимает заново. Команда shell ждёт карточку подтверждения."
            }

            Button {
                Layout.alignment: Qt.AlignRight
                text: "Закрыть"
                onClicked: overlayRoot.desk.helpOpen = false
            }
        }
        }
    }
}
