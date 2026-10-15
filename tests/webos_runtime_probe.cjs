'use strict';
// Trusted acceptance probe. Run only inside webos_runtime_check.py's OS sandbox.
// The DOM implements the project's existing controls, not a browser layout engine.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = fs.realpathSync(process.argv[2]);
const bootstrap = String.raw`
  const stored = new Map(), ids = new Map();
  globalThis.localStorage = {
    getItem(k) { return stored.get(k) ?? null; },
    setItem(k, v) { stored.set(k, String(v)); },
    removeItem(k) { stored.delete(k); }, clear() { stored.clear(); }
  };
  class Element {
    constructor(tag = 'div') {
      this.tagName = tag.toUpperCase(); this.children = []; this.value = '';
      this.className = ''; this.textContent = ''; this.style = {}; this.dataset = {};
      this.scrollHeight = 0; this._html = '';
      this.classList = {add: (...v) => this.className += ' ' + v.join(' '), remove() {}, toggle() {}};
    }
    appendChild(child) { this.children.push(child); child.parentNode = this; return child; }
    append(...children) { children.forEach(child => this.appendChild(child)); }
    setAttribute(key, value) { this[key] = value; if (key === 'id') ids.set(value, this); }
    addEventListener(name, fn) { this['on' + name] = fn; }
    remove() { if (this.parentNode) this.parentNode.children = this.parentNode.children.filter(x => x !== this); }
    focus() {}
    click() { this.onclick?.({target: this, stopPropagation() {}, preventDefault() {}}); }
    set innerHTML(html) {
      this._html = String(html); this.children = [];
      const tokens = this._html.match(/<[^>]+>|[^<]+/g) || [], stack = [this];
      for (const token of tokens) {
        if (token.startsWith('</')) { if (stack.length > 1) stack.pop(); continue; }
        if (!token.startsWith('<')) { stack.at(-1).textContent += token; continue; }
        const tag = token.match(/^<([\w-]+)/)?.[1]; if (!tag) continue;
        const child = new Element(tag);
        for (const match of token.matchAll(/([\w-]+)=["']([^"']*)["']/g)) {
          const key = match[1] === 'class' ? 'className' : match[1];
          child[key] = match[2]; if (key === 'id') ids.set(match[2], child);
        }
        stack.at(-1).appendChild(child);
        if (!['input', 'br', 'hr', 'img', 'meta', 'link'].includes(tag)) stack.push(child);
      }
    }
    get innerHTML() { return this._html; }
    querySelectorAll(selector) {
      const matches = node => selector.startsWith('#') ? node.id === selector.slice(1) :
        selector.startsWith('.') ? node.className.split(/\s+/).includes(selector.slice(1)) :
        node.tagName.toLowerCase() === selector.toLowerCase();
      return this.children.flatMap(child => [...(matches(child) ? [child] : []), ...child.querySelectorAll(selector)]);
    }
    querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
  }
  globalThis.document = {
    createElement: tag => new Element(tag), getElementById: id => ids.get(id) || null,
    querySelector: selector => selector.startsWith('#') ? ids.get(selector.slice(1)) || null : null
  };
  globalThis.window = globalThis;
  globalThis.prompt = () => 'acceptance-note.txt';
  globalThis.alert = () => {}; globalThis.confirm = () => true;
  globalThis.console = {log() {}, error() {}, warn() {}};
  globalThis.assert = (value, message) => { if (!value) throw new Error(message); };
  globalThis.contents = (fs, name) => {
    let node = fs.root;
    for (const part of name.split('/').filter(Boolean)) node = node?.children?.[part];
    return node?.type === 'file' ? node.content : null;
  };
`;
const cases = {
  terminal_help: `const out = document.createElement('div'); new TerminalApp(kernel).execute('help', out);
    assert(out.children.some(x => x.textContent.includes('ls')), 'help must list commands');`,
  terminal_ls: `const out = document.createElement('div'); new TerminalApp(kernel).execute('ls', out);
    assert(out.children.some(x => x.textContent.includes('documents') && x.textContent.includes('notes.txt')), 'ls must list root');`,
  terminal_cat: `const out = document.createElement('div'); new TerminalApp(kernel).execute('cat notes.txt', out);
    assert(out.children.some(x => x.textContent.includes('Welcome to WebOS!')), 'cat must read notes.txt');`,
  terminal_touch: `new TerminalApp(kernel).execute('touch acceptance.txt', document.createElement('div'));
    assert(contents(new VirtualFS(), 'acceptance.txt') === '', 'touch must persist an empty file');`,
  terminal_rm: `const term = new TerminalApp(kernel); term.execute('touch acceptance.txt', document.createElement('div'));
    assert(contents(kernel.fs, 'acceptance.txt') === '', 'rm precondition');
    term.execute('rm acceptance.txt', document.createElement('div'));
    assert(contents(new VirtualFS(), 'acceptance.txt') === null, 'rm must delete persisted file');`,
  root_readdir: `assert(typeof kernel.fs.readdir === 'function', 'readdir missing');
    const rows = kernel.fs.readdir('');
    assert(Array.isArray(rows) && rows.includes('documents') && rows.includes('notes.txt'), 'empty path must list root');`,
  notepad_read: `new NotepadApp(kernel).render(document.createElement('div'), {filename: 'notes.txt'});
    assert(document.getElementById('note-editor').value === 'Welcome to WebOS!', 'notepad must open file');`,
  notepad_save: `new NotepadApp(kernel).render(document.createElement('div'), {});
    document.getElementById('note-editor').value = 'acceptance persisted text';
    document.getElementById('note-save').click();
    assert(contents(new VirtualFS(), 'acceptance-note.txt') === 'acceptance persisted text', 'notepad must persist save');
    new NotepadApp({fs: new VirtualFS()}).render(document.createElement('div'), {filename: 'acceptance-note.txt'});
    assert(document.getElementById('note-editor').value === 'acceptance persisted text', 'saved note must reopen');`,
  todo_persistence: `new TodoApp(kernel).render(document.createElement('div'));
    document.getElementById('new-task').value = 'acceptance todo'; document.getElementById('add-task-btn').click();
    const saved = JSON.parse(contents(new VirtualFS(), 'documents/todo.json'));
    assert(Array.isArray(saved) && saved[0]?.text === 'acceptance todo', 'todo must persist new item');
    new TodoApp({fs: new VirtualFS()}).render(document.createElement('div'));
    let item = document.getElementById('todo-list').children[0]; assert(item, 'todo must reload saved item');
    item.querySelector('span').click();
    assert(JSON.parse(contents(new VirtualFS(), 'documents/todo.json'))[0].completed === true, 'todo completion must persist');
    item = document.getElementById('todo-list').children[0]; item.querySelector('.del-task').click();
    assert(JSON.parse(contents(new VirtualFS(), 'documents/todo.json')).length === 0, 'todo deletion must persist');`,
  explorer_open: `const launched = []; kernel.apps = {launch: (...args) => launched.push(args)};
    new FileExplorerApp(kernel).render(document.createElement('div'));
    const list = document.getElementById('explorer-files'); assert(list && list.children.length >= 2, 'explorer must list root');
    const note = list.children.find(item => item.querySelectorAll('span').some(span => span.textContent === 'notes.txt'));
    assert(note, 'explorer must display notes.txt'); note.click();
    assert(launched.some(x => x[0] === 'notepad' && x[1]?.filename === 'notes.txt'), 'explorer must open notes in notepad');`
};
async function run(body) {
  const context = vm.createContext(Object.create(null), {codeGeneration: {strings: false, wasm: false}});
  vm.runInContext(bootstrap, context, {timeout: 1500});
  const modules = new Map();
  const load = async filename => {
    const resolved = fs.realpathSync(filename);
    if (!resolved.startsWith(root + path.sep)) throw new Error('module outside fixture');
    if (modules.has(resolved)) return modules.get(resolved);
    const mod = new vm.SourceTextModule(fs.readFileSync(resolved, 'utf8'), {context, identifier: resolved});
    modules.set(resolved, mod);
    await mod.link((specifier, owner) => {
      if (!specifier.startsWith('.')) throw new Error('external module denied');
      return load(path.resolve(path.dirname(owner.identifier), specifier));
    });
    return mod;
  };
  for (const [file, name] of [['core/fs', 'VirtualFS'], ['apps/terminal', 'TerminalApp'],
    ['apps/notepad', 'NotepadApp'], ['apps/todo', 'TodoApp'], ['apps/explorer', 'FileExplorerApp']]) {
    const mod = await load(path.join(root, 'src', file + '.js'));
    await mod.evaluate({timeout: 1500}); context[name] = mod.namespace[name];
  }
  vm.runInContext('const kernel = {fs: new VirtualFS()};\n' + body, context, {timeout: 1500});
}
(async () => {
  const checks = {};
  for (const [name, body] of Object.entries(cases)) {
    try { await run(body); checks[name] = {passed: true}; }
    catch (e) { checks[name] = {passed: false, error: e.name + ': ' + e.message}; }
  }
  console.log(JSON.stringify({checks, passed: Object.values(checks).every(x => x.passed),
    coverage: 'Node ESM methods + DOM controls and localStorage; no browser layout/end-to-end UI'}, null, 2));
})().catch(e => { console.error(e.message); process.exitCode = 2; });
