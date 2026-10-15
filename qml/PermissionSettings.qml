pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    Layout.fillWidth: true
    implicitHeight: shellCol.implicitHeight + 28
    ColumnLayout {
        id: shellCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 10
        Text {
            text: "КОМАНДЫ"
            color: Theme.gold
            font.family: Theme.mono
            font.pixelSize: Theme.fontSize(11)
            font.letterSpacing: 1.6
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            text: "Одна строка — начало команды. git пускает git status и не пускает git stash. Пока «разрешить все» выключено, пустой список спрашивает каждую команду, а «всегда спросить» важнее списка разрешения. Когда «разрешить все» включено, карточки нет — кроме удаления, если включён тумблер удаления."
        }
        ToggleLine {
            label: "Разрешить все команды"
            on: root.desk.shellAllowAll
            onFlipped: {
                root.desk.shellAllowAll = !root.desk.shellAllowAll
                root.desk.markDirty()
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.desk.shellAllowAll ? Theme.gold : Theme.muted
            font.pixelSize: Theme.fontSize(12)
            text: root.desk.shellAllowAll
                  ? "После сохранения команды будут выполняться без подтверждения, кроме правил «Всегда спрашивать» и защищённого удаления."
                  : "После сохранения для команд вне списка разрешения потребуется подтверждение."
        }
        ToggleLine {
            label: "Удаление только с разрешения"
            on: root.desk.deleteConfirm
            onFlipped: {
                root.desk.deleteConfirm = !root.desk.deleteConfirm
                root.desk.markDirty()
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: Theme.fontSize(12)
            text: "Когда тумблер удаления включён, rm, rmdir, unlink, find -delete и git clean спрашивают, даже если все команды разрешены. Запись через >, tee и sed -i по-прежнему отклоняется: для файлов есть edit_file."
        }
        RuleBox {
            label: "Разрешать без вопроса"
            hint: "Например: git status, git diff, git log, ls, rg"
            text: root.desk.shellAuto
            onTextChanged: {
                if (text === root.desk.shellAuto)
                    return
                root.desk.shellAuto = text
                root.desk.markDirty()
            }
        }
        RuleBox {
            label: "Всегда спрашивать"
            hint: "Например: git push, git reset"
            text: root.desk.shellAsk
            onTextChanged: {
                if (text === root.desk.shellAsk)
                    return
                root.desk.shellAsk = text
                root.desk.markDirty()
            }
        }
        ToggleLine {
            label: "Только чтение (без правок файлов)"
            on: root.desk.readOnly
            onFlipped: {
                root.desk.readOnly = !root.desk.readOnly
                root.desk.markDirty()
            }
        }
        ToggleLine {
            label: "Сеть в shell (curl и т.п.)"
            on: root.desk.shellNet
            onFlipped: {
                root.desk.shellNet = !root.desk.shellNet
                root.desk.markDirty()
            }
        }
        ToggleLine {
            label: "Shell без bubblewrap (небезопасно)"
            on: root.desk.allowUnsandboxedShell
            onFlipped: {
                root.desk.allowUnsandboxedShell = !root.desk.allowUnsandboxedShell
                root.desk.markDirty()
            }
        }
        Field {
            label: "Таймаут shell, сек"
            text: String(root.desk.shellTimeoutSec)
            onCommitted: (v) => {
                const n = parseInt(v, 10)
                if (!isNaN(n) && n > 0) {
                    root.desk.shellTimeoutSec = n
                    root.desk.markDirty()
                }
            }
        }
        RuleBox {
            label: "Не показывать в списках"
            hint: "Имя каталога на строку. Пусто — node_modules, .git, dist, .next, out, __pycache__, .venv, venv, coverage, .cache, vendor, target. list_dir, rg, ls и find их прячут, пока команда сама не называет каталог."
            text: root.desk.listIgnore
            onTextChanged: {
                if (text === root.desk.listIgnore)
                    return
                root.desk.listIgnore = text
                root.desk.markDirty()
            }
        }
    }
}
