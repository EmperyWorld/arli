import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Rectangle {
    id: root
    required property var desk
    anchors.fill: parent
    visible: open
    color: "#d007060b"
    property bool open: false
    property int cursor: 0
    objectName: "commandPalette"
    signal cycleLayout()
    signal hardStopRequested()

    function toggle() {
        open = !open
        if (open) {
            query.text = ""
            cursor = 0
            query.forceActiveFocus()
        }
    }

    function entries() {
        const q = query.text.trim().toLowerCase()
        const rows = []
        const acts = [
            ["Новый диалог", "Ctrl+N", function () { root.desk.createDialog(); root.desk.page = 0 }],
            ["Обновить списки", "", function () { root.desk.refresh() }],
            ["Открыть чат", "Ctrl+1", function () { root.desk.page = 0 }],
            ["Открыть настройки", "Ctrl+2", function () { root.desk.page = 1 }],
            ["Кто такая Арли", "Ctrl+3", function () { root.desk.page = 2 }],
            ["Сменить компоновку", "Ctrl+\\", function () { root.cycleLayout() }],
            ["Остановить ход", "Esc", function () { root.desk.cancel() }, root.desk.busy],
            ["Жёсткая остановка", "", function () { root.hardStopRequested() }, root.desk.busy],
            ["Сменить тему", "", function () { Theme.mode = Theme.light ? "dark" : "light" }],
            ["Компактные отступы", "", function () { Theme.dense = !Theme.dense }],
            ["Подсказки", "?", function () { root.desk.helpOpen = true }]
        ]
        for (let i = 0; i < acts.length; ++i) {
            if (!q.length || acts[i][0].toLowerCase().indexOf(q) >= 0)
                rows.push({ kind: "act", title: acts[i][0], hint: acts[i][1], run: acts[i][2], enabled: acts[i].length < 4 || acts[i][3], id: 0 })
        }
        const n = root.desk.dialogs.rowCount()
        for (let i = 0; i < n; ++i) {
            const title = root.desk.dialogs.titleAt(i)
            if (!q.length || title.toLowerCase().indexOf(q) >= 0)
                rows.push({
                    kind: "dialog",
                    enabled: true,
                    title: title,
                    hint: "диалог",
                    id: root.desk.dialogs.idAt(i),
                    run: function () {}
                })
        }
        return rows
    }

    function go(row) {
        if (!row || !row.enabled)
            return
        if (row.kind === "dialog")
            root.desk.openDialog(row.id)
        else
            row.run()
        root.open = false
        if (row.kind === "dialog") root.desk.page = 0
    }

    function move(delta) {
        const n = list.model.length
        if (!n)
            return
        cursor = (cursor + delta + n) % n
        list.positionViewAtIndex(cursor, ListView.Contain)
    }

    MouseArea { anchors.fill: parent; onClicked: root.open = false }

    Panel {
        width: Math.min(parent.width - 80, 640)
        height: Math.min(parent.height - 80, 520)
        anchors.centerIn: parent
        fill: Theme.panel
        border.color: Theme.goldDim

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: "Команды и диалоги"; color: Theme.ink; font.pixelSize: Theme.fontSize(18); font.bold: true }
                Button { text: "×"; implicitWidth: 34; Accessible.name: "Закрыть палитру команд"; onClicked: root.open = false }
            }
            TextField {
                id: query
                objectName: "commandQuery"
                Layout.fillWidth: true
                placeholderText: "Действие или название диалога"
                color: Theme.ink
                placeholderTextColor: Theme.faint
                font.pixelSize: Theme.fontSize(16)
                selectByMouse: true
                background: Rectangle {
                    color: Theme.bg
                    radius: 8
                    border.color: Theme.goldDim
                }
                onTextChanged: root.cursor = 0
                Keys.onUpPressed: root.move(-1)
                Keys.onDownPressed: root.move(1)
                Keys.onEscapePressed: root.open = false
                Keys.onReturnPressed: root.go(list.model[root.cursor])
                Keys.onEnterPressed: root.go(list.model[root.cursor])
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                model: root.open ? root.entries() : []
                delegate: Rectangle {
                    id: entryRow
                    required property int index
                    required property var modelData
                    width: list.width
                    height: Theme.dense ? 40 : 48
                    opacity: entryRow.modelData.enabled ? 1 : 0.45
                    radius: 8
                    color: entryRow.index === root.cursor ? Theme.goldWash : "transparent"
                    border.color: entryRow.index === root.cursor ? Theme.goldDim : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        Text {
                            text: entryRow.modelData.kind === "dialog" ? "диалог" : "действие"
                            color: Theme.faint
                            font.family: Theme.mono
                            font.pixelSize: Theme.fontSize(10)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: entryRow.modelData.title
                            color: Theme.ink
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSize(14)
                        }
                        Text {
                            text: entryRow.modelData.hint
                            color: Theme.gold
                            font.family: Theme.mono
                            font.pixelSize: Theme.fontSize(10)
                        }
                    }
                    TapHandler { enabled: entryRow.modelData.enabled; onTapped: root.go(entryRow.modelData) }
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - 32
                    visible: list.count === 0
                    text: "Ничего не найдено. Попробуйте название действия или диалога."
                    wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    color: Theme.muted; font.pixelSize: Theme.fontSize(14)
                }
            }
            Text {
                Layout.fillWidth: true
                text: "↑ ↓ выбрать · Enter выполнить · Esc закрыть"
                color: Theme.faint; font.pixelSize: Theme.fontSize(11)
            }
        }
    }
}
