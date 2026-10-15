%{!?arli_version:%{error:Define arli_version from VERSION using scripts/build-rpm.sh}}
Name:           arli
Version:        %{arli_version}
Release:        1%{?dist}
Summary:        Local C++ agent — orchestrator, worker and Qt shell
License:        MIT
URL:            https://github.com/emperyworld/arli
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(grpc++)
BuildRequires:  pkgconfig(protobuf)
BuildRequires:  grpc-plugins
BuildRequires:  protobuf-devel
BuildRequires:  openssl-devel
BuildRequires:  libcurl-devel
BuildRequires:  sqlite-devel
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  systemd-rpm-macros
Requires:       grpc
Requires:       protobuf
Requires:       libcurl
Requires:       sqlite
Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       ripgrep
Requires:       bubblewrap
Requires:       git

%description
Arli is a local agent: arli-daemon (orchestrator on 127.0.0.1:4792),
arli-agent (LLM and tools) and arli-shell (Qt 6 Quick). No web UI.
State lives in $XDG_DATA_HOME/arli. Start with `arli` or
`systemctl --user enable --now arli-daemon`.

%prep
%autosetup -n %{name}-%{version}

%build
cmake -S src -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=%{_prefix} \
  -DARLI_BUILD_SHELL=ON \
  -DARLI_BUILD_TESTS=ON
cmake --build build

%install
cmake --install build --prefix %{buildroot}%{_prefix}

%check
ctest --test-dir build --output-on-failure

%post
%systemd_user_post arli-daemon.service

%preun
%systemd_user_preun arli-daemon.service

%postun
%systemd_user_postun arli-daemon.service

%files
%license LICENSE NOTICE
%doc README.md
%{_bindir}/arli
%{_bindir}/arli-daemon
%{_bindir}/arli-agent
%{_bindir}/arli-job-supervisor
%{_bindir}/arli-shell
%{_bindir}/arli-probe
%{_bindir}/arli-doctor
%{_libdir}/libarli_embed.a
%{_libdir}/libarli_common.a
%{_libdir}/libarli_proto.a
%{_libdir}/libarli_grpc_tools.a
%{_libdir}/cmake/Arli/
%{_includedir}/arli/
%{_datadir}/arli/
%{_datadir}/applications/arli.desktop
%{_datadir}/icons/hicolor/scalable/apps/arli.svg
%{_userunitdir}/arli-daemon.service

%changelog
* Wed Oct 14 2026 EmperyWorld <dev@empery.world> - 0.5.5-1
- Add a native Qwen3 profile, deterministic clarification and bounded tool budgets.
- Add checked multi-file replacement and reject verification commands that mask failures.
- Page turn traces, bound settings and gRPC payloads, and preserve queued work across restart.
- Add Qwen live acceptance and ASan/UBSan/TSan release gates.

* Sun Oct 11 2026 EmperyWorld <dev@empery.world> - 0.5.4-1
- Report actual JavaScript candidate errors and add strict fresh-read fragment replacement.
- Preserve rejected edits and checked milestones across turns; report partial completion honestly.
- Add configurable model temperature with queued-task snapshots and authenticated gRPC transport.
- Expose an installed host Node binary to sandboxed checks without exposing the user home.

* Sun Oct 11 2026 EmperyWorld <dev@empery.world> - 0.5.3-1
- Add bounded conversation and dialog pages, Unicode search and stale-response fencing.
- Add durable file journals, confirmed recovery, workspace identity and acceptance checks.
- Harden authenticated gRPC, task/stream limits, private state and background jobs.
- Ship relocatable SDK libraries and expanded daemon, Qt and live-model regression coverage.

* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.5.2-1
- Add expanded tool result viewer with text search, zoom and clipboard feedback.
- Collapse long replies and link compact changed-file cards to matching results.
- Fix review layout, modal keyboard handling and file-card visibility.

* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.5.1-1
- Add full conversation search, workspace layout buttons and conversation copying.
- Improve keyboard dialog navigation, command palette and reduced motion preferences.

* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.5.0-1
- Resizable workspace panels and persistent light/dark, text scale and density settings
- Render fenced code with exact copy actions and separate colored diffs from tool output
- Improve operation labels, error filtering and scrollable decision cards on small windows
- Split settings into focused components and extend responsive UI validation
* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.4.8-1
- Report the exact unread interval and read_file arguments instead of generic retries
- Explain capped source windows; distinguish stale snapshots and invalid ranges
- Reproduce a 542-line edit blocked until the missing 401-542 tail is read
* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.4.7-1
- Edit inclusive line ranges only after successful reads of all affected lines
- Invalidate read permissions after writes and reject externally changed snapshots
- Require fresh reads of the file tail for append; retain guarded legacy edits
* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.4.6-1
- Prefer edits anchored to unique source fragments over changing line offsets
- Keep legacy line/text APIs; test rejection of stale source anchors
- Enforce structured Gemma action JSON and keep source reads proportional to context
- Repeat known checks after writes and reject success claims while checks fail
- Reject observed LLM protocol leakage before writing source files
* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.4.5-1
- Declare complete edit coordinates before the large text payload; keep legacy aliases
- Preserve UTF-8 boundaries when clipping large tool arguments for audit
- Verify exact writes of a 539-line file and its backup
* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.4.4-1
- Repair unparsed Gemma tool tags through the model, never execute text as tools
- Exclude failed call text from later turns and recognize improvement requests
- Regression coverage for recovery and persistent format failures
* Sat Oct 10 2026 EmperyWorld <dev@empery.world> - 0.4.3-1
- Fit tool history before every model request, including the first large observation
- Align documented client and server context settings for the local Gemma stack
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.4.1-1
- Reject source line gutters and edits without changes
- Fix sandbox mounts and command runtime paths
- Gemma recommended sampling, larger reasoning budget and live web acceptance
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.4.0-1
- Responsive native UI, selectable messages and complete tool output
- Organized settings, guarded composer and hard-stop confirmation
- Reliable file tools, context recovery and offline UI regression tests
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.20-1
- Emperion embed: AgentPage, libarli_embed, plan/memory modes, edit_stats UI
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.19-1
- Stop explore thrash: reject repeat reads, force real append chunks, nudge writes
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.18-1
- Desk stays on screen: visible scrollbars, pinned composer, readable thoughts
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.17-1
- Premium desk: quieter chrome, conversation-first chat, clearer settings state
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.16-1
- edit_file is a short window; grow existing files with append_file
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.15-1
- Context compression no longer cuts a UTF-8 character and kills the turn
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.14-1
- Settings switches stay put; a second daemon cannot share the listen port
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.13-1
- Context meter and compression at 80 percent, premium controls, toggles save immediately
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.12-1
- Background jobs in the desk, allow-all and delete toggles, hide node_modules from listings
* Fri Oct 09 2026 EmperyWorld <dev@empery.world> - 0.3.11-1
- Shell allow and ask lists, live turn counters, chat column with a ten-message window
* Thu Oct 08 2026 EmperyWorld <dev@empery.world> - 0.3.10-1
- Persona page, command palette, reading layouts; Arli's role in the worker prompt
* Thu Oct 08 2026 EmperyWorld <dev@empery.world> - 0.3.9-1
- Shell desk: dialogs, live tokens, inspector, settings, hotkeys
* Thu Oct 08 2026 EmperyWorld <dev@empery.world> - 0.3.8-1
- First packaged build: daemon, worker, Qt shell, user unit, doctor
