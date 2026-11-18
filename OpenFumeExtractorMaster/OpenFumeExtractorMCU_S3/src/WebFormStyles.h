#pragma once

#define OFE_WEB_FORM_STYLE R"FORM(<style>
input[type=file]{width:100%;min-width:0;height:44px;min-height:44px;padding:0;line-height:42px;overflow:hidden;font-size:13px}
input[type=file]::file-selector-button{height:42px;margin:0 10px 0 0;padding:0 12px;border:0;border-right:1px solid var(--line);border-radius:6px 0 0 6px;background:var(--card2);color:var(--text);font:inherit;cursor:pointer;transition:background-color .15s,color .15s}
input[type=file]:enabled::file-selector-button:hover{background:#343d48;color:#fff}input[type=file]:disabled::file-selector-button{cursor:not-allowed}
.ofe-file-actions{display:grid;grid-template-columns:minmax(0,1fr);align-items:center;gap:12px 8px;min-width:0;margin:0}.ofe-file-actions>.ofe-usb-action{justify-self:start;max-width:100%;height:44px;min-height:44px;margin:0!important;padding:9px 12px;font-size:13px}
.ofe-export-actions{display:inline-flex;align-items:center;flex-wrap:wrap;gap:12px 8px;max-width:100%;vertical-align:middle}.ofe-export-actions>button,.ofe-export-actions>.btn{min-height:40px;height:40px;margin:0!important;padding:9px 12px;line-height:20px;font-size:13px}.ofe-export-actions>.ofe-usb-action{white-space:nowrap}
.ofe-file-actions+button,.ofe-file-actions+.ofe-file-actions,.ofe-export-actions+.ofe-file-actions{margin-top:12px}
body :is(.upb-actions,.mb-builder-actions,.uni-dual-action){display:flex;flex-wrap:wrap;align-items:stretch;gap:10px;min-width:0}
body :is(.upb-actions,.mb-builder-actions,.uni-dual-action)>button{flex:1 1 165px;width:auto;min-width:0}
body :is(.upb-actions,.mb-builder-actions,.uni-dual-action)>.ofe-export-actions{display:flex;flex:2 1 300px;align-items:stretch;gap:10px;width:auto;min-width:0;max-width:100%}
body :is(.upb-actions,.mb-builder-actions,.uni-dual-action)>.ofe-export-actions>button{flex:1 1 140px;width:auto;min-width:0}
body :is(.upb-actions,.mb-builder-actions,.uni-dual-action) button{height:44px;min-height:44px;margin:0!important;padding:6px 10px;font-size:12px;line-height:16px;white-space:normal;overflow-wrap:anywhere}
.jbu-station-live>.jbu-port-detail-grid{margin-top:7px}.jbu-service-read{padding:0 12px 12px;min-width:0}.jbu-service-actions{display:flex;flex-wrap:wrap;gap:10px;align-items:stretch}.jbu-service-actions>select{flex:1 1 180px;min-width:0}.jbu-service-actions>button{height:44px;min-height:44px;flex:0 0 auto;margin:0}
@media(max-width:600px){.ofe-file-actions>.ofe-usb-action{width:100%}.ofe-export-actions{width:100%;display:flex}.ofe-export-actions>*{flex:1 1 auto}}
</style>)FORM"
