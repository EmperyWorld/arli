import QtQuick
import QtQuick.Controls
import Arli

ScrollView {
    id: root
    property string source: ""
    property bool wrap: false
    property bool highlightDiff: false
    property real textZoom: 1
    property var matches: []
    property int matchCursor: 0
    readonly property string selectedText: code.selectedText
    function find(needle, direction) {
        const text = code.getText(0, code.length).toLowerCase()
        const term = needle.toLowerCase()
        const positions = []
        if (term.length) {
            let at = text.indexOf(term)
            while (at >= 0) { positions.push(at); at = text.indexOf(term, at + term.length) }
        }
        matches = positions
        matchCursor = direction && positions.length ? (matchCursor + direction + positions.length) % positions.length : 0
        if (positions.length) {
            code.cursorPosition = positions[matchCursor]
            code.select(positions[matchCursor], positions[matchCursor] + needle.length)
        } else code.deselect()
    }
    readonly property string formatted: {
        return "<pre>" + source.split("\n").map(function(line) {
            const escaped = line.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
            const color = line.startsWith("@@") ? Theme.cyan :
                (line.startsWith("+++") || line.startsWith("---") ? Theme.gold :
                (line.startsWith("+") ? Theme.emerald : (line.startsWith("-") ? Theme.crimson : Theme.ink)))
            return '<span style="color:' + color + '">' + escaped + '</span>'
        }).join("\n") + "</pre>"
    }
    clip: true
    TextArea {
        id: code
        text: root.highlightDiff ? root.formatted : root.source
        textFormat: root.highlightDiff ? TextEdit.RichText : TextEdit.PlainText
        readOnly: true
        selectByMouse: true
        wrapMode: root.wrap ? TextArea.Wrap : TextArea.NoWrap
        color: Theme.ink
        selectionColor: Theme.gold
        selectedTextColor: Theme.bg
        font.family: Theme.mono
        font.pixelSize: Math.round(Theme.fontSize(12) * root.textZoom)
        background: null
        Accessible.name: "Полный результат инструмента"
    }
}
