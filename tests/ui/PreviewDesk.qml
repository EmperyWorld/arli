import QtQuick

// Isolated UI fixture: no daemon, network, secrets or filesystem actions.
QtObject {
    id: root
    property int selectedTrace: -1
    property int copyCount: 0
    property string lastCopied: ""
    property int sendCount: 0
    property int stopCount: 0
    property int hardStopCount: 0
    signal textCopied()
    signal askChanged()
    property bool daemonUp: true
    property string statusText: ""
    property string sliceText: ""
    property string versionText: "preview"
    property string workerState: "idle"
    property int workerPid: 0
    property int queueDepth: 0
    property double dialogId: 1
    property string dialogTitle: "Улучшение проекта"
    property bool busy: false
    property string draft: ""
    property string dialogFilter: ""
    property string llmBaseUrl: "http://127.0.0.1:8080/v1"
    property string llmModel: "gemma-4-26B"
    property string llmProfile: "gemma4"
    property bool llmReachable: true
    property string workdir: "/home/user/projects/example"
    property bool hasApiKey: false
    property int turnTimeout: 600
    property int idleTimeout: 120
    property int cancelGrace: 5
    property string shellAuto: ""
    property string shellAsk: ""
    property bool shellAllowAll: false
    property bool deleteConfirm: true
    property string listIgnore: ""
    property int contextTokens: 16384
    property int maxSteps: 64
    property int shellTimeoutSec: 60
    property bool shellNet: false
    property bool readOnly: false
    property bool allowUnsandboxedShell: false
    property int contextUsed: 0
    property int contextWindow: 0
    property bool contextCompressed: false
    property var filePass: ({active: false, hold: false, doneCount: 0, head: "", paths: []})
    property var plan: ({active: false, failed: false, current: 0, steps: []})
    property int streamChars: 0
    property string turnEditSummary: ""
    property var jobs: []
    property bool settingsDirty: false
    property string settingsState: "Настройки сохранены"
    property bool confirmOpen: false
    property string confirmKind: ""
    property string confirmToken: ""
    property string confirmCommand: ""
    property string confirmConsequence: ""
    property bool askOpen: false
    property string askQuestion: ""
    property var askOptions: []
    property int page: 0
    property bool helpOpen: false
    property bool jobLogSelected: false
    function stopJobLog() { jobLogSelected = false }
    property string inspectorTitle: ""
    property string inspectorBody: ""
    property string inspectorDiff: ""
    property double shownTurn: 0
    property var messageSteps: []
    required property var dialogs
    required property var messages
    required property var trace
    required property var tasks
    function send() { sendCount++; draft = "" }
    function cancel() { stopCount++ }
    function hardStop() { hardStopCount++ }
    function createDialog() { dialogId = 1; page = 0 }
    function openDialog(id) { dialogId = id; page = 0 }
    function markDirty() { settingsDirty = true }
    function saveSettings() { settingsDirty = false }
    function selectTrace(index) { selectedTrace = index; inspectorTitle = "edit_file · успешно"; inspectorBody = "OK\nФайл обновлён"; inspectorDiff = "--- app.cpp\n+++ app.cpp\n@@ -1 +1 @@\n-old\n+new" }
    function copyText(value) { copyCount++; lastCopied = value; textCopied() }
    function renameDialog(value) {}
    function deleteDialog() {}
    function refresh() {}
    function setApiKey(value) {}
    function clearApiKey() {}
    function confirmDanger(allow) {}
    function answerAsk(answer) {}
    function showNote(title, body) {}
    function showTurn(id) { shownTurn = shownTurn === id ? 0 : id }
    function revertTurn(id) {}
    function openJobLog(id) {}
    function stopJob(id) {}
    function stopAllJobs() {}
    function clearFilePass() {}
    function clearPlan() {}
}
