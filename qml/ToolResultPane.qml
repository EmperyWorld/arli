pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

ColumnLayout {
    id: root
    required property var desk
    property bool showDiff: false
    property bool wrap: false
    property real zoom: 1
    property bool allowExpand: true
    readonly property bool diffActive: showDiff && desk.inspectorDiff.length > 0
    readonly property string source: diffActive ? desk.inspectorDiff : desk.inspectorBody
    signal expandRequested()
    signal escapeRequested()
    function clearSearch() { if (!query.activeFocus || !query.text.length) return false; query.text = ""; return true }
    function focusSearch() { query.forceActiveFocus(); query.selectAll() }
    spacing: 8
    Text {
        Layout.fillWidth: true
        text: root.desk.inspectorTitle || "Результат шага"
        color: Theme.cyan; font.pixelSize: Theme.fontSize(13); font.bold: true; elide: Text.ElideRight
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 2
        Button { objectName: "toolOutputButton"; text: "Вывод"; flat: true; checked: !root.diffActive; onClicked: root.showDiff = false }
        Button { objectName: "toolDiffButton"; text: "Изменения"; flat: true; checked: root.diffActive; enabled: root.desk.inspectorDiff.length > 0; onClicked: root.showDiff = true }
        Button { text: "↵"; implicitWidth: 34; flat: true; checked: root.wrap; Accessible.name: "Перенос строк"; ToolTip.visible: hovered; ToolTip.text: "Перенос строк"; onClicked: root.wrap = !root.wrap }
        Item { Layout.fillWidth: true }
        Button { objectName: "expandToolResult"; text: "⤢"; implicitWidth: 34; flat: true; visible: root.allowExpand; enabled: root.source.length > 0; Accessible.name: "Расширенный просмотр результата"; ToolTip.visible: hovered; ToolTip.text: "Расширенный просмотр"; onClicked: root.expandRequested() }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        TextField {
            id: query
            objectName: "toolResultSearch"
            Layout.fillWidth: true; Layout.minimumWidth: 70
            placeholderText: "Найти в результате"
            Accessible.name: "Поиск в выводе или изменениях"
            onTextChanged: result.find(text, 0)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    result.find(text, (event.modifiers & Qt.ShiftModifier) ? -1 : 1); event.accepted = true
                }
            }
            Keys.onEscapePressed: root.escapeRequested()
        }
        Text { objectName: "toolResultMatchCount"; visible: query.text.length > 0; text: result.matches.length ? (result.matchCursor + 1) + "/" + result.matches.length : "0"; color: result.matches.length ? Theme.muted : Theme.amber; font.pixelSize: Theme.fontSize(11) }
        Button { text: "↓"; implicitWidth: 30; flat: true; enabled: result.matches.length > 0; Accessible.name: "Следующее совпадение в результате"; onClicked: result.find(query.text, 1) }
    }
    DiffView {
        id: result
        objectName: "toolResultView"
        Layout.fillWidth: true; Layout.fillHeight: true
        wrap: root.wrap; textZoom: root.zoom; highlightDiff: root.diffActive; source: root.source
        onSourceChanged: Qt.callLater(function() { result.find(query.text, 0) })
    }
    RowLayout {
        Layout.fillWidth: true
        Button {
            objectName: "copyToolResultButton"
            text: "Копировать"; flat: true
            ToolTip.visible: hovered; ToolTip.text: root.diffActive ? "Копировать изменения" : "Копировать вывод"
            Layout.fillWidth: true; enabled: root.source.length > 0
            onClicked: root.desk.copyText(root.source)
        }
        Button { text: "−"; implicitWidth: 30; flat: true; enabled: root.zoom > 0.85; Accessible.name: "Уменьшить текст результата"; onClicked: root.zoom = Math.max(0.85, root.zoom - 0.1) }
        Button { text: Math.round(root.zoom * 100) + "%"; implicitWidth: 64; flat: true; Accessible.name: "Сбросить масштаб результата"; onClicked: root.zoom = 1 }
        Button { text: "+"; implicitWidth: 30; flat: true; enabled: root.zoom < 1.6; Accessible.name: "Увеличить текст результата"; onClicked: root.zoom = Math.min(1.6, root.zoom + 0.1) }
    }
    Text { Layout.fillWidth: true; text: root.source.length ? root.source.split("\n").length + " строк · " + root.source.length + " знаков" : "Выберите действие на вкладке «Шаги»."; color: Theme.faint; font.pixelSize: Theme.fontSize(10); wrapMode: Text.Wrap }
}
