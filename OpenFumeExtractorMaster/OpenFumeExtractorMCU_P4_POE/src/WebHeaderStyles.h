#pragma once

// Lucide code-xml icon, ISC; docs/licenses/Lucide-ISC.txt.
#define OFE_WEB_HEADER_STYLE R"HEADER(<style>
body>main{max-width:1600px}
body>main:has(>.status-modules){max-width:none}
@media(max-width:700px){
body>header{padding:8px 10px;min-height:0;gap:6px;align-items:stretch}
body>header .brand{display:grid;grid-template-columns:28px minmax(0,1fr) auto auto;width:100%;gap:6px;align-items:center}
body>header .brand-logo{width:28px;height:28px;border-radius:6px}
body>header .brand-text{min-width:0;font-size:14px;line-height:1.15;overflow-wrap:anywhere}
body>header .brand-text>span{font-size:10px;margin-top:2px}
body>header .brand-dev{grid-column:3;grid-row:1;width:40px;height:40px;min-height:40px;margin:0;padding:0;font-size:0;gap:0}
body>header .brand-dev::before{content:"";width:20px;height:20px;background:currentColor;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m18 16 4-4-4-4M6 8l-4 4 4 4M14.5 4l-5 16'/%3E%3C/svg%3E") center/contain no-repeat}
body.dev-mode>header .brand-dev,body>header .brand-dev[data-enabled="1"]{color:var(--green);border-color:var(--green)}
body>header .brand .transport-icons{grid-column:4;grid-row:1;gap:0;margin:0}
body>header .transport-icons a{width:36px;height:40px}
body>header .nav{display:flex;flex-wrap:nowrap;gap:6px;width:100%;min-width:0;overflow-x:auto;overscroll-behavior-x:contain;scroll-padding-inline:6px;scrollbar-width:thin}
body>header .nav>*{flex:0 0 auto;width:auto;min-height:40px;height:40px;margin:0;padding:8px 11px;font-size:12px;line-height:22px;white-space:nowrap}
body>header .nav select{max-width:110px}
}
</style>)HEADER"
