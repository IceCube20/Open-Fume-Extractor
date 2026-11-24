#pragma once

#define OFE_WEB_THEME_STYLE R"THEME(<style>
:root[data-ofe-theme=dark]{color-scheme:dark}
:root[data-ofe-theme=light]{color-scheme:light;--bg:#cbd3db;--card:#e1e6eb;--card2:#d4dde5;--line:#a6b4c1;--text:#22313f;--muted:#465767;--blue:#2b648f;--green:#236f48;--field:#d0dae4;--control:#bfccd8;--control-hover:#b2c2d1;--section:#ced8e1}
html[data-ofe-theme=light] body>header{background:var(--card);border-color:var(--line)}
html[data-ofe-theme=light] :is(.panel,.tile,.overview-group,.control-panel,.module-card,.logic-panel,.diag-panel,.diag-module-card,.fanio-editor-shell,.fanio-editor-panel,.usb-dialog,#usb-picker){background:var(--card);border-color:var(--line);color:var(--text)}
html[data-ofe-theme=light] :is(.stat,.stat-cell,.jbu-fact,.module-system,.fanio-channel,.uni-form-section,.mb-builder-panel,.usb-toolbar,.usb-selection,.usb-global){background:var(--card2);border-color:var(--line);color:var(--text)}
html[data-ofe-theme=light] :is(.section-title,.module-head,.fanio-editor-head,.fanio-editor-foot,.diag-module-head,.usb-list-head,thead,th){background:var(--section);border-color:var(--line);color:var(--text)}
html[data-ofe-theme=light] :is(.status-hero,.jbu-hero,.jbu-port-details,.jbu-chip,.logic-enable,.logic-live,.logic-chip,.logic-symbol){background:var(--card2);border-color:var(--line);color:var(--text)}
html[data-ofe-theme=light] :is(input:not([type=checkbox]):not([type=radio]):not([type=range]):not([type=color]):not(.inline-alias),select,textarea,pre){background:var(--field);color:var(--text);border-color:#a9b6c5}
html[data-ofe-theme=light] :is(input,select,textarea):disabled{background:#c7d0d9;color:#596977}
html[data-ofe-theme=light] :is(button.secondary,.btn.secondary){background:var(--control);border-color:#94a7b8;color:#273e51}
html[data-ofe-theme=light] :is(button.secondary,.btn.secondary):hover{background:var(--control-hover)}
html[data-ofe-theme=light] .nav .active{background:#236da7;color:#fff;border-color:#236da7}
html[data-ofe-theme=light] :is(.muted,.k,label,.module-type,.module-address,.module-footer,.mini,.logic-note,.logic-mode-status,.logic-status,.fanio-pin-help,.fanio-editor-message){color:var(--muted)}
html[data-ofe-theme=light] :is(.v,.section-title,.module-title,.jbu-station-name,.jbu-port-detail-item strong,.control-copy strong,.logic-node h3,.logic-node .body,.logic-connection-row,.fanio-editor-shell h3){color:var(--text)}
html[data-ofe-theme=light] :is(td,.footer,.routing-rule,.rule-head,.module-footer,.module-system,.module-head,.logic-node h3){border-color:var(--line)}
html[data-ofe-theme=light] .alarm-item{background:var(--state-warn-bg);color:var(--state-warn-fg)}
html[data-ofe-theme=light] .alarm-item.critical{background:var(--state-error-bg);color:var(--state-error-fg)}
html[data-ofe-theme=light] .alarm-item.ok{background:var(--state-ok-bg);color:var(--state-ok-fg)}
html[data-ofe-theme=light] .pill.on{background:var(--state-ok-bg);color:var(--state-ok-fg)}
html[data-ofe-theme=light] .pill.warn{background:var(--state-warn-bg);color:var(--state-warn-fg)}
html[data-ofe-theme=light] .pill.critical{background:var(--state-error-bg);color:var(--state-error-fg)}
html[data-ofe-theme=light] .logic-workspace{background:#d8e0e7;border-color:var(--line)}
html[data-ofe-theme=light] .logic-canvas{background:radial-gradient(circle,#c4cdd7 1px,transparent 1px);background-size:18px 18px}
html[data-ofe-theme=light] .logic-node{background:var(--card);border-color:#99a9ba;box-shadow:0 5px 16px #18232e26}
html[data-ofe-theme=light] .logic-line-halo{stroke:#d8e0e7}
html[data-ofe-theme=light] .logic-line{stroke:#64778b}
html[data-ofe-theme=light] .logic-line.is-on{stroke:#168048}
html[data-ofe-theme=light] .logic-line.live-flow-on{stroke:#236da7}
html[data-ofe-theme=light] .logic-line.selected{stroke:#9d6909}
html[data-ofe-theme=light] .logic-line.route-blocked{stroke:#b43c3c}
html[data-ofe-theme=light] .logic-selected{outline-color:#a66c00}
html[data-ofe-theme=light] .logic-segment-dot{background:#a66c00;border-color:#fff}
html[data-ofe-theme=light] .logic-selection-count{color:#835600}
html[data-ofe-theme=light] .logic-mobile-tabs{background:var(--bg)}
html[data-ofe-theme=light] :is(.mb-modal-window,.upb-editor,.modbus-builder,.universal-profile-body){background:var(--card);color:var(--text);border-color:var(--line)}
html[data-ofe-theme=light] :is(.mb-modal-head,.mb-row,.mb-advanced,.mb-preview,.upb-frame,.upb-exchange){background:var(--card2);color:var(--text);border-color:var(--line)}
html[data-ofe-theme=light] :is(.mb-modal-title-copy strong,.mb-row-title,.mb-row strong,.upb-editor strong){color:var(--text)}
html[data-ofe-theme=light] :is(.mb-modal-title-copy small,.mb-intro,.mb-reg-note,.logic-port-name,.logic-node small){color:var(--muted)}
html[data-ofe-theme=light] .logic-mode-label.sim{color:#168048}
html[data-ofe-theme=light] input[type=file]::file-selector-button{background:var(--control);color:#273e51;border-color:#94a7b8}
html[data-ofe-theme=light] input[type=file]:hover::file-selector-button{background:var(--control-hover)}
/* Shared surfaces also cover page-specific styles loaded after the shell. */
:root[data-ofe-theme=light]{--surface-inset:#d6dfe7;--surface-hover:#c5d2de;--state-ok-bg:#c4dbce;--state-ok-fg:#205f3c;--state-warn-bg:#e4d5b4;--state-warn-fg:#684c15;--state-error-bg:#e2c7cc;--state-error-fg:#802c3c;--state-info-bg:#c7d8e8;--state-info-fg:#24567f}
:root[data-ofe-theme=light] :is(table,.jbu-port-card,.jbu-station-card,.jbu-tech,.jbu-port-detail-item,.jbu-station-metric,.jbu-port-metric,.jbu-state-card,.jbu-transport,.metric-line div,.devid,.module-meta-grid,.module-meta-grid>div,.module-card>.metric-line,.module-card .module-system,.module-card .module-footer,.module-card .status-note code,.module-card .status-note span,.universal-card .status-note span,.led-pair,.uni-chip,.universal-profile,.universal-debug,.universal-debug .uni-debug-stats>div,.pro-cal,.pro-cal .cal-reading,.logic-summary-card,.logic-summary-chip,.uni-file-label,.fanio-editor-section,.fanio-editor-launch){background:var(--surface-inset);border-color:var(--line);color:var(--text)}
:root[data-ofe-theme=light] :is(.module-card>.module-head,.jbu-station-metrics,.rule-number,.route-summary,.logic-block-tags span,.alarm-count,.jbu-chip,.jbu-chip.proto,.jbu-chip.kind,.jbu-port-future,.jbu-port-state.idle,.jbu-flag){background:var(--surface-inset);border-color:var(--line);color:var(--text)}
:root[data-ofe-theme=light] :is(.modbus-builder-launch,.modbus-builder-launch-open,.mb-modal-icon,.mb-row-num,.mb-reg-note,.upb-launch,.upb-launch-open,.upb-intro,.upb-help-card,.upb-row,.upb-row-num,.upb-rule-note,.upb-preview,.builder-output-assistant){background:var(--surface-inset);border-color:var(--line);color:var(--text)}
:root[data-ofe-theme=light] :is(.modbus-builder-launch,.upb-launch,.fanio-editor-launch):hover{background:var(--surface-hover);border-color:#9eb2c5}
:root[data-ofe-theme=light] :is(.diag-master-led-pair,.diag-led-pair,.diag-local-summary,.diag-status-metric,.diag-errors-line,.diag-device-section,.diag-dir,.diag-status,.diag-mini-badge,.diag-mode-switch,.diag-trace-switch,.diag-local-stat,.diag-flow-node,.diag-proto-field,.diag-session-state,.diag-session-state.stopped,.diag-mode-card,.diag-filterbar,.diag-filter-chip,.diag-trace-legend,.diag-payload details[open],.diag-jbcusb-stat,.diag-metric-chip,.jbc-meta-badge,.jbc-result-role,.jbc-route-node,.jbc-value,.jbc-byte-field,.jbc-format-badge,.proto-meta-badge,.proto-result-value,.proto-raw-field){background:var(--surface-inset);border-color:var(--line);color:var(--text)}
:root[data-ofe-theme=light] :is(.diag-mode-card:hover,.diag-search-clear:hover){background:var(--surface-hover);border-color:#9eb2c5}
:root[data-ofe-theme=light] :is(.diag-module-name,.diag-device-title,.diag-device-detail,.diag-proto-ascii,.diag-data,.data,.jbu-port-detail-item strong,.jbu-station-metric strong,.mb-row-title,.mb-modal-title-copy strong,.modbus-builder-launch-copy strong,.upb-launch-copy strong,.upb-row-title,.builder-output-assistant strong,.pro-cal strong){color:var(--text)}
:root[data-ofe-theme=light] :is(.diag-module-type,.diag-status-metric .k,.diag-errors-line>.k,.diag-mode-copy small,.diag-mode-copy span,.diag-local-stat,.jbc-value.bool-off,.jbc-format-badge,.proto-meta-badge,.jbu-port-card small,.jbu-transport,.jbu-flag,.mb-intro,.mb-reg-note,.upb-intro,.upb-help-card,.upb-rule-note,.pro-cal .muted){color:var(--muted)}
:root[data-ofe-theme=light] :is(.brand span,.off:not(.switch),.wifi-db,.ssid,.ethernet-label small,.module-card .control-copy small,.module-card .status-note,.module-card .module-system small,.jbu-state-copy small,.jbu-port-title small,.jbu-port-details summary,.jbu-station-copy small,.jbu-station-copy span,.jbu-station-title span,.jbu-counter-grid,.jbu-tech summary,.jbu-tech-grid strong,.footer,.uni-chip span,.uni-chip small,.universal-entity small,.uni-descriptor summary,.uni-empty,.pro-cal .cal-sub,.pro-cal .cal-reading small,.pro-cal .cal-advanced summary,.pro-cal .cal-grid label,.panel-heading p,.routing-master,.rule-head,.logic-summary-sub,.logic-summary-chip small,.uni-section-title small,.uni-live-note,.uni-help,.universal-debug .uni-debug-stats span,.universal-card .universal-entity small,.modbus-builder>summary small,.mb-builder-head small,.mb-advanced>summary,.mb-builder-msg,.mb-empty,.mb-limit-note,.modbus-builder-launch-copy small,.upb-launch-copy small,.upb-builder-head small,.upb-advanced>summary,.upb-empty,.upb-msg,.upb-preview>summary,.upb-limit-note,.builder-output-title small,.builder-output-note,.fanio-editor-launch small,.fanio-editor-head small,.fanio-empty){color:var(--muted)}
:root[data-ofe-theme=light] .module-card.core-style :is(.core-control-title,.range-head){color:var(--muted)}
:root[data-ofe-theme=light] :is(.diag-badge,.diag-mode-icon,.diag-proto-command,.diag-proto-command.jbc,.jbc-result-role.request,.jbc-frame-dir.tx,.jbu-station-mark,.kind-fe .jbu-station-mark,.jbu-port-state.cooling,.jbu-flag.cool){background:var(--state-info-bg);color:var(--state-info-fg);border-color:#b5cee8}
:root[data-ofe-theme=light] :is(.diag-badge.local,.diag-mode-icon.local,.diag-status.ok,.diag-online-pill.is-online,.diag-session-state.running,.diag-check-badge.ok,.diag-metric-chip.ok,.proto-meta-badge.ok,.jbc-result-role.response,.jbc-frame-dir.rx,.jbc-value.bool-on,.jbu-state-card.is-on,.jbu-flag.on,.jbu-error,.jbu-station-state.ok,.jbu-station-error,.kind-ha .jbu-station-mark,.kind-cl .jbu-station-mark,.fanio-input-state.is-active,.diag-proto-command.modbus){background:var(--state-ok-bg);color:var(--state-ok-fg);border-color:#b6d9c3}
:root[data-ofe-theme=light] :is(.msg,.diag-status.warn,.diag-online-pill.is-warning,.diag-metric-chip.warn,.proto-meta-badge.warn,.jbc-value.warn,.jbu-state-card.is-stand.is-on,.jbu-port-state.stand,.jbu-flag.stand,.jbu-station-state.offline,.kind-ph .jbu-station-mark,.diag-proto-command.weller,.diag-proto-command.universal){background:var(--state-warn-bg);color:var(--state-warn-fg);border-color:#dfc992}
:root[data-ofe-theme=light] :is(.alarm-count.active,.module-card .pill.critical,.diag-status.err,.diag-online-pill.is-offline,.diag-jbcusb-stat.err,.diag-metric-chip.err,.diag-check-badge.err,.proto-meta-badge.err,.diag-control-error,.jbu-error.bad,.jbu-station-state.fault,.jbu-station-error.is-bad,.mb-reg-note.err){background:var(--state-error-bg);color:var(--state-error-fg);border-color:#e4b4bb}
:root[data-ofe-theme=light] :is(.diag-check-badge.neutral,.diag-mini-badge.diag-online-pill:not(.is-online):not(.is-offline):not(.is-warning),.fanio-input-state:not(.is-active)){background:var(--surface-inset);color:var(--muted);border-color:var(--line)}
:root[data-ofe-theme=light] :is(.kind-sf .jbu-station-mark,.jbu-flag.pedal){background:#f0e9f7;color:#704297;border-color:#d4c2e3}
:root[data-ofe-theme=light] :is(.tx,.uart_tx,.jbc-value.primary,.diag-payload summary,.diag-txn-details summary,.diag-raw-details summary){color:var(--state-info-fg)}
:root[data-ofe-theme=light] :is(.rx,.uart_rx,.jbc-len-ok,.diag-local-stat.ok,.diag-delta.fast,.diag-eco-banner,.diag-eco-sprout){color:var(--state-ok-fg)}
:root[data-ofe-theme=light] :is(.timeout,.jbc-len-bad,.diag-local-stat.err){color:var(--state-error-fg)}
:root[data-ofe-theme=light] :is(.info,.diag-local-stat.warn,.diag-delta.slow){color:var(--state-warn-fg)}
:root[data-ofe-theme=light] :is(.bar:not(.progress>.bar),.wifi-bar,.progress,.diag-share-track,.diag-bus-share-value .diag-share-track){background:#d2dae3;border-color:var(--line)}
:root[data-ofe-theme=light] .module-card input[type=range]::-webkit-slider-runnable-track{background:#d2dae3}
:root[data-ofe-theme=light] .jbu-toggle-track{background:#8c9bab;border-color:#7a8c9e}
:root[data-ofe-theme=light] .jbu-config input[type=checkbox]:not(:checked){background:radial-gradient(circle at 12px 50%,#fff 0,#fff 9px,transparent 10px),#8c9bab;border-color:#7a8c9e}
:root[data-ofe-theme=light] .dev-fw-override{background:var(--state-warn-bg)!important;color:var(--state-warn-fg)!important;border-color:#dfc992!important}
:root[data-ofe-theme=light] :is(.password-modal-backdrop,.mb-modal-backdrop,.fanio-editor-modal){background:rgba(40,54,70,.28)}
:root[data-ofe-theme=light] :is(#usb-picker,.usb-dialog)::backdrop{background:rgba(40,54,70,.28)}
:root[data-ofe-theme=light] :is(.module-card,.diag-module-card){background:var(--card)!important;border-color:var(--line)!important;box-shadow:0 4px 14px #18232e12!important}
:root[data-ofe-theme=light] .jbu-station-error{background:var(--state-ok-bg)!important;border-color:#b6d9c3!important}
:root[data-ofe-theme=light] .jbu-station-error.is-bad{background:var(--state-error-bg)!important;border-color:#e4b4bb!important}
:root[data-ofe-theme=light] .jbu-station-error.is-bad .v{color:var(--state-error-fg)!important}
:root[data-ofe-theme=light] .builder-output-toggle{color:var(--text)!important}
:root[data-ofe-theme=light] .secondary{background:var(--control);border-color:#94a7b8;color:#273e51}
:root[data-ofe-theme=light] .secondary:hover{background:var(--control-hover)}
:root[data-ofe-theme=light] .secondary.danger{color:var(--state-error-fg);border-color:#d9a6ad}
:root[data-ofe-theme=light] :is(.module-card strong,.universal-card strong,.module-card .module-title,.module-card>.devid .mini,.jbu-state-copy strong,.jbu-fact .v,.jbu-port-head strong,.jbu-port-metric strong,.jbu-port-future strong,.jbu-config>summary,.jbu-toggle-row,.jbu-config-head>label,.jbu-station-copy strong,.jbu-station-title,.jbu-table td,.jbu-cartridge-live strong,.jbu-counter-grid strong,.role-lines span,code,.uni-chip strong,.universal-profile summary,.universal-debug summary,.universal-entity strong,.universal-card .uni-section-title .k,.universal-card .universal-entity .v,.pro-cal .cal-title,.pro-cal strong,.logic-summary-title,.logic-summary-chip strong,.uni-section-title,.uni-form-title,.universal-debug .uni-debug-stats strong,.modbus-builder>summary,.mb-builder-head strong,.mb-row-title strong,.upb-intro strong,.upb-help-card strong,.upb-builder-head strong,.upb-row-title strong,.builder-output-title strong,.fanio-editor-section>summary,.fanio-editor-launch strong,.diag-jbcusb-title strong,.diag-jbcusb-stat .v,.proto-raw-field .v,.diag-module-title){color:var(--text)}
:root[data-ofe-theme=light] :is(.warn,.logic-mode-status.warn,.logic-limit-warning,.logic-capacity .at-limit,.logic-router-status,.jbu-config-status.is-saving,.uni-save-state.is-dirty,.mb-builder-msg.warn,.upb-msg.warn,#usb-storage .usb-directory .usb-name>svg){color:var(--state-warn-fg)}
:root[data-ofe-theme=light] :is(.jbu-config-status.is-error,.mb-builder-msg.err,.upb-msg.err,.fanio-editor-message.is-error,.diag-wifi-timing-control>[role=alert],#usb-picker .usb-picker-error,#usb-storage .usb-feedback[data-error=true],#usb-storage .usb-dialog-error){color:var(--state-error-fg)}
:root[data-ofe-theme=light] .logic-live.offline{color:var(--state-error-fg);border-color:#e4b4bb}
:root[data-ofe-theme=light] .jbu-ha-levels .is-selected strong{color:var(--state-ok-fg)}
:root[data-ofe-theme=light] .transport-icons a:hover{color:var(--state-info-fg)}
:root[data-ofe-theme=light] .module-card.core-style .jbu-state-card.metric-only .core-glyph{background:var(--module-tint,var(--card2));color:var(--module-ink,var(--text))}
:root[data-ofe-theme=light] :is(.on:not(.switch),.module-eco-badge,.master-eco-sprout,.jbu-config-status,.jbu-ha-levels .is-selected,.uni-save-state.is-ok,.mb-builder-msg.ok,.upb-msg.ok,.fanio-channel-index){color:var(--state-ok-fg)}
:root[data-ofe-theme=light] :is(.rule-arrow,.signal-arrow,.jbu-transport .usb-glyph){color:var(--state-info-fg)}
:root[data-ofe-theme=light] .jbu-transport strong{color:var(--text)}
:root[data-ofe-theme=light] :is(.jbc-route-arrow,.diag-flow-arrow,.diag-jbcusb-note,.diag-arrow,.diag-module-address){color:var(--muted)}
:root[data-ofe-theme=light] .diag-mode-check{color:var(--state-ok-fg)}
:root[data-ofe-theme=light] .logic-mode-label{color:var(--muted)}
:root[data-ofe-theme=light] .logic-mode-label.sim.active{color:var(--state-ok-fg)}
:root[data-ofe-theme=light] .logic-mode-label.live.active{color:var(--state-info-fg)}
#ofe_theme{width:auto;min-width:84px;max-width:120px}
:root[data-ofe-theme=light] .progress>.bar{background:var(--green);height:100%;border-radius:inherit}
:root[data-ofe-theme=light] .switch:enabled:hover{filter:brightness(1.14)}
:root[data-ofe-theme=light] .switch:disabled{filter:none;cursor:not-allowed}
:root[data-ofe-theme=light] .pro-cal .cal-badge{display:inline-flex;align-items:center;min-height:24px;border:1px solid #b59e69;background:var(--state-warn-bg);color:var(--state-warn-fg)}
:root[data-ofe-theme=light] .pro-cal .cal-badge.is-ready{background:var(--state-ok-bg);border-color:#88aa96;color:var(--state-ok-fg)}
:root[data-ofe-theme=light] .pro-cal .cal-badge:is(.is-disabled,.is-offline){background:#c3cfda;border-color:#96a8b9;color:var(--muted)}
:root[data-ofe-theme=light] .diag-module-icon .module-head-glyph{color:inherit}
:root[data-ofe-theme=light] .logic-node.is-on{background:#cce0d3;border-color:#42805c;box-shadow:0 0 0 2px #42805c50,0 0 16px #42805c35,0 5px 16px #18232e26}
:root[data-ofe-theme=light] .logic-node.live-on:not(.is-on){box-shadow:0 0 0 2px #3b719a55,0 0 16px #3b719a38,0 5px 16px #18232e26}
:root[data-ofe-theme=light] .logic-node.live-flow-on{background:#cbddec;border-color:#3b719a;box-shadow:0 0 0 2px #3b719a55,0 0 16px #3b719a38,0 5px 16px #18232e26}
:root[data-ofe-theme=light] .logic-node.is-on .logic-symbol{color:var(--state-ok-fg);border-color:#88aa96}
:root[data-ofe-theme=light] .logic-node:is(.live-on:not(.is-on),.live-flow-on) .logic-symbol{color:var(--state-info-fg);border-color:#8ca8c2}
:root[data-ofe-theme=light] .logic-node.is-on h3{border-color:#88aa96}
:root[data-ofe-theme=light] .logic-node.live-flow-on h3{border-color:#8ca8c2}
/* Component identity and control states must not inherit generic text colors. */
:root[data-ofe-theme=light] :is(.jbc-card,.diag-module-card.jbc,.diag-module-card.jbcusb,.module-list-icon[data-module-type="1"],.module-list-icon[data-module-type="9"]){--module-ink:#17663e;--module-tint:#c6d9ce}
:root[data-ofe-theme=light] :is(.fan-card,.diag-module-card.fan,.module-list-icon[data-module-type="2"],.module-list-icon[data-module-type="3"]){--module-ink:#245f92;--module-tint:#c7d8e7}
:root[data-ofe-theme=light] :is(.weller-card,.diag-module-card.weller,.module-list-icon[data-module-type="5"]){--module-ink:#196478;--module-tint:#c5dadd}
:root[data-ofe-theme=light] :is(.display-card,.diag-module-card.display,.module-list-icon[data-module-type="6"]){--module-ink:#674397;--module-tint:#d5cde3}
:root[data-ofe-theme=light] :is(.rs232-card,.diag-module-card.universal,.module-list-icon[data-module-type="7"]){--module-ink:#775713;--module-tint:#dfd4bd}
:root[data-ofe-theme=light] :is(.modbus-card,.diag-module-card.modbus,.module-list-icon[data-module-type="8"]){--module-ink:#8c461e;--module-tint:#e0cdbf}
:root[data-ofe-theme=light] :is(.module-card .module-icon,.module-list-icon,.diag-module-icon){background:var(--module-tint,var(--card2));color:var(--module-ink,var(--text));border:1px solid var(--line)}
:root[data-ofe-theme=light] .logic-chip,:root[data-ofe-theme=light] .logic-symbol{color:var(--state-info-fg);--logic-bubble:var(--card)}
:root[data-ofe-theme=light] .logic-node[data-type=input] .logic-symbol{color:var(--state-ok-fg)}
:root[data-ofe-theme=light] .logic-node[data-type=output] .logic-symbol{color:var(--module-ink,var(--state-info-fg))}
:root[data-ofe-theme=light] :is(.pill,.diag-online-pill,.jbu-port-state,.jbu-station-state,.diag-session-state){border-radius:999px;border:1px solid #96a8b9;background:#c3cfda;color:var(--muted);line-height:1;white-space:nowrap}
:root[data-ofe-theme=light] :is(.pill.on,.diag-online-pill.is-online,.jbu-station-state.ok,.diag-session-state.running){background:var(--state-ok-bg);border-color:#88aa96;color:var(--state-ok-fg)}
:root[data-ofe-theme=light] :is(.pill.warn,.diag-online-pill.is-warning,.jbu-port-state.stand,.jbu-station-state.offline){background:var(--state-warn-bg);border-color:#b59e69;color:var(--state-warn-fg)}
:root[data-ofe-theme=light] :is(.pill.critical,.diag-online-pill.is-offline,.jbu-station-state.fault){background:var(--state-error-bg);border-color:#ba8d97;color:var(--state-error-fg)}
:root[data-ofe-theme=light] .jbu-port-state.cooling{background:var(--state-info-bg);border-color:#8ca8c2;color:var(--state-info-fg)}
:root[data-ofe-theme=light] .transport-icons a{color:var(--muted)}
:root[data-ofe-theme=light] .transport-icons .transport-ble.is-on{color:var(--state-info-fg)}
:root[data-ofe-theme=light] .transport-icons :is(.transport-network.is-on,.transport-usb.is-on){color:var(--state-ok-fg)}
:root[data-ofe-theme=light] .transport-icons :is(.transport-network.is-ap,.transport-usb:not(.is-on)){color:var(--state-warn-fg)}
:root[data-ofe-theme=light] .module-card .inline-alias{color:var(--text)}
:root[data-ofe-theme=light] .module-card .inline-alias:focus{background:var(--field);border-color:var(--blue)}
:root[data-ofe-theme=light] .module-card .metric-line div{background:transparent}
:root[data-ofe-theme=light] .module-card :is(.metric-line,.devid,.module-meta-grid,.module-meta-grid>div,.display-settings>.metric-line>div){background:transparent}
:root[data-ofe-theme=light] .module-card :is(.metric-line,.devid,.control-list,.control-row,.status-note,.module-meta-grid,.range-control,.core-control-title,.module-system,.module-system span,.module-footer,.jbu-config,.jbu-port-details){border-color:var(--line)}
:root[data-ofe-theme=light] :is(button,.btn){box-shadow:none}
:root[data-ofe-theme=light] :is(button,.btn):focus-visible{outline:2px solid var(--blue);outline-offset:2px}
:root[data-ofe-theme=light] .switch{--switch-off:#8d9eae;--switch-on:var(--module-ink,var(--green));--switch-inset:inset 0 0 0 1px #6f8497;background:var(--switch-off);box-shadow:var(--switch-inset);color:transparent}
:root[data-ofe-theme=light] .switch.on{background:var(--switch-on);color:transparent}
:root[data-ofe-theme=light] .module-card input[type=range]::-webkit-slider-runnable-track{background:#b6c5d2}
:root[data-ofe-theme=light] .module-card input[type=range]::-webkit-slider-thumb{border-color:var(--module-ink,var(--blue));background:#edf1f4;box-shadow:0 1px 3px #31475a35}
:root[data-ofe-theme=light] .module-card input[type=range]::-moz-range-track{background:#b6c5d2}
:root[data-ofe-theme=light] .module-card input[type=range]::-moz-range-thumb{border-color:var(--module-ink,var(--blue));background:#edf1f4;box-shadow:0 1px 3px #31475a35}
:root[data-ofe-theme=light] .module-card.core-style .jbu-state-card.is-on{background:var(--state-ok-bg);border-color:#88aa96;color:var(--state-ok-fg)}
:root[data-ofe-theme=light] .module-card.core-style .jbu-state-card.is-stand.is-on{background:var(--state-warn-bg);border-color:#b59e69;color:var(--state-warn-fg)}
:root[data-ofe-theme=light] .module-card.core-style .jbu-state-card.is-on :is(.jbu-state-symbol,.jbu-state-copy strong){color:inherit}
.fanio-led-zone{border-top:1px solid var(--line);margin-top:16px;padding-top:16px}
.fanio-led-zone input[type=color]{height:42px;min-width:0;padding:4px;background:var(--card);border:1px solid var(--line)}
.fanio-led-zone .fanio-field-grid>button:last-child{width:44px;min-width:44px;justify-self:end;align-self:end}
.fanio-led-zone+.fanio-led-zone{margin-top:20px}
html[data-ofe-theme=light] .fanio-editor-panel h3{color:var(--text)}
</style><script>
(()=>{const key='ofe-web-theme',root=document.documentElement,system=matchMedia('(prefers-color-scheme: dark)');let mode='auto';try{const stored=localStorage.getItem(key);if(['auto','light','dark'].includes(stored))mode=stored}catch(_){}
const apply=()=>{root.dataset.ofeTheme=mode==='auto'?(system.matches?'dark':'light'):mode};apply();system.addEventListener('change',apply);
addEventListener('DOMContentLoaded',()=>{const nav=document.querySelector('body>header .nav');if(!nav)return;const control=document.createElement('select');control.id='ofe_theme';for(const value of ['auto','light','dark'])control.add(new Option(value,value));control.value=mode;const labels=()=>{const de=document.getElementById('lang_sel')?.value==='de';control.title=de?'Darstellung':'Appearance';control.setAttribute('aria-label',control.title);[...control.options].forEach(o=>o.textContent=o.value==='auto'?'Auto':o.value==='light'?(de?'Hell':'Light'):(de?'Dunkel':'Dark'))};control.onchange=()=>{mode=control.value;try{localStorage.setItem(key,mode)}catch(_){}apply()};nav.prepend(control);labels();document.addEventListener('change',e=>{if(e.target.id==='lang_sel')labels()})});
})();
</script>)THEME"
