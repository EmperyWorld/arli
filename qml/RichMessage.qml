pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Arli

Column {
    id: root
    required property var desk
    property string source: ""
    property bool plain: false
    spacing: 12
    readonly property var fragments: {
        if (plain) return [{code: false, text: source}]
        const result = []
        const re = /```([^\n`]*)\n([\s\S]*?)(?:```|$)/g
        let match, last = 0
        while ((match = re.exec(source)) !== null) {
            if (match.index > last) result.push({code: false, text: source.slice(last, match.index)})
            result.push({code: true, language: match[1].trim() || "Код", text: match[2].replace(/\n$/, "")})
            last = re.lastIndex
        }
        if (last < source.length) result.push({code: false, text: source.slice(last)})
        return result.length ? result : [{code: false, text: " "}]
    }
    Repeater {
        model: root.fragments
        delegate: Column {
            id: fragment
            required property var modelData
            width: root.width
            TextEdit {
                width: parent.width
                visible: !fragment.modelData.code
                height: visible ? contentHeight : 0
                text: fragment.modelData.text
                readOnly: true; selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: root.plain ? TextEdit.PlainText : TextEdit.MarkdownText
                color: Theme.ink; selectionColor: Theme.gold; selectedTextColor: Theme.bg
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize(15)
                onLinkActivated: (link) => { if (/^https?:\/\//.test(link)) Qt.openUrlExternally(link) }
            }
            Rectangle {
                width: parent.width
                visible: fragment.modelData.code
                height: visible ? codeHeader.height + codeScroll.height + 14 : 0
                radius: Theme.radiusSm; color: Theme.codeBg; border.color: Theme.border
                Row {
                    id: codeHeader
                    height: 38; x: 12; width: parent.width - 24
                    Text { width: parent.width - copyCode.width; height: parent.height; verticalAlignment: Text.AlignVCenter; text: fragment.modelData.language || "Код"; font.family: Theme.mono; font.pixelSize: Theme.fontSize(11); color: Theme.muted }
                    Button { id: copyCode; objectName: fragment.modelData.code ? "copyCodeButton" : ""; text: "Копировать код"; flat: true; implicitHeight: 34; onClicked: root.desk.copyText(fragment.modelData.text) }
                }
                ScrollView {
                    id: codeScroll
                    x: 12; y: codeHeader.height; width: parent.width - 24
                    height: Math.min(280, Math.max(24, codeText.contentHeight + 10))
                    clip: true
                    TextArea {
                        id: codeText
                        text: fragment.modelData.text; readOnly: true; selectByMouse: true
                        wrapMode: TextArea.NoWrap; background: null
                        color: Theme.ink; selectionColor: Theme.gold; selectedTextColor: Theme.bg
                        font.family: Theme.mono; font.pixelSize: Theme.fontSize(13)
                        Accessible.name: "Код в ответе Арли"
                    }
                }
            }
        }
    }
}
