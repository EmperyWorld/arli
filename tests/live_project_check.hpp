#pragma once
// Behavior regression from the installed project's Kernel class/instance collision.
inline constexpr const char* kProjectCheck = R"JS(
const assert = require('node:assert/strict');
const fs = require('node:fs'), vm = require('node:vm');
const store = new Map();
const context = {console, localStorage: {
  getItem: key => store.get(key) ?? null,
  setItem: (key,value) => store.set(key,value)
}, document: {getElementById: () => ({})}};
context.window = context;
vm.createContext(context);
vm.runInContext(fs.readFileSync('script.js','utf8'), context, {timeout:1000});
const kernel = context.Kernel;
assert.equal(typeof kernel.init, 'function', 'window.Kernel is the application instance');
assert.equal(typeof context.onload, 'function', 'startup handler preserved');
let started = false;
kernel.init = () => {started = true;};
context.onload();
assert.ok(started, 'load handler must start the instance, not the class');
assert.ok(vm.runInContext('Kernel.fs === window.Kernel.fs', context), 'inline handlers resolve the same instance');
assert.ok(kernel.fs.createFile('check.txt'));
assert.ok(kernel.fs.writeFile('check.txt','preserved file API'));
assert.equal(kernel.fs.root.children['check.txt'].content, 'preserved file API');
assert.ok(kernel.fs.mkdir('checkdir'));
kernel.ui.updateTaskbar = () => {};
kernel.processManager.startProcess('test', {name:'Test'});
assert.equal(kernel.processManager.getRunningApps().length, 1);
kernel.processManager.stopProcess('test');
assert.equal(kernel.processManager.getRunningApps().length, 0);
for (const id of ['todo','explorer','terminal','notepad','browser','settings'])
  assert.ok(kernel.apps.apps.some(app => app.id === id), `app ${id} preserved`);
console.log('PROJECT_RUNTIME_OK');
)JS";
