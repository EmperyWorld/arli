pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Panel {
    id: root
    objectName: "conversationSearch"
    required property var messages
    property bool opened: false
    property var matches: []
    property int cursor: 0
    readonly property int matchCount: matches.length
    property bool previousHistory: false
    readonly property int selectedRow: opened && matches.length ? matches[cursor] : -1
    signal locate(int sourceRow)
    signal closed()
    visible: opened
    implicitHeight: content.implicitHeight + 16
    function start() {
        if (!opened) previousHistory = messages.historyOpen
        opened = true
        messages.historyOpen = true
        query.forceActiveFocus()
        query.selectAll()
        refresh()
    }
    function stop() {
        opened = false
        query.text = ""
        matches = []
        messages.historyOpen = previousHistory
        closed()
    }
    function refresh() {
        if (!opened) return
        messages.historyOpen = true
        const needle = query.text.trim().toLowerCase()
        const rows = []
        if (needle.length)
            for (let i = 0; i < messages.messageCount; ++i)
                if (messages.bodyAt(i).toLowerCase().indexOf(needle) >= 0) rows.push(i)
        matches = rows
        cursor = Math.min(cursor, Math.max(0, rows.length - 1))
        if (rows.length) locate(rows[cursor])
    }
    function move(delta) {
        if (!matches.length) return
        cursor = (cursor + delta + matches.length) % matches.length
        locate(matches[cursor])
    }
    Connections {
        target: root.messages
        function onModelReset() { if (root.opened) debounce.restart() }
        function onCountChanged() { if (root.opened) debounce.restart() }
        function onDataChanged() { if (root.opened) debounce.restart() }
    }
    Timer { id: debounce; interval: 180; onTriggered: root.refresh() }
    RowLayout {
        id: content
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 8; spacing: 6
        TextField {
            id: query
            objectName: "conversationSearchInput"
            Layout.fillWidth: true; Layout.minimumWidth: 80
            placeholderText: "Найти в переписке"
            Accessible.name: "Поиск по всем загруженным сообщениям"
            onTextChanged: { root.cursor = 0; debounce.restart() }
            Keys.onEscapePressed: root.stop()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.move((event.modifiers & Qt.ShiftModifier) ? -1 : 1); event.accepted = true
                }
            }
        }
        Text {
            objectName: "conversationSearchCount"
            text: query.text.trim().length ? (root.matches.length ? (root.cursor + 1) + " / " + root.matches.length : "Нет совпадений") : ""
            color: root.matches.length ? Theme.muted : Theme.amber; font.pixelSize: Theme.fontSize(11)
        }
        Button { text: "↑"; implicitWidth: 34; enabled: root.matches.length > 0; Accessible.name: "Предыдущее сообщение"; onClicked: root.move(-1) }
        Button { objectName: "conversationSearchNext"; text: "↓"; implicitWidth: 34; enabled: root.matches.length > 0; Accessible.name: "Следующее сообщение"; onClicked: root.move(1) }
        Button { objectName: "conversationSearchClose"; text: "×"; implicitWidth: 34; Accessible.name: "Закрыть поиск"; onClicked: root.stop() }
    }
}
