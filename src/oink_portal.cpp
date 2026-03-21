#include "oink_portal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include <cstring>

#include "oink_config.h"
#include "oink_gnss.h"
#include "oink_log.h"
#include "oink_rtc.h"
#include "oink_scan.h"
#include "oink_settings.h"
#include "oink_state.h"
#include "oink_time.h"
#include "oink_uplink.h"

namespace oink {
namespace portal {
namespace {

AsyncWebServer gServer(80);
DNSServer gDnsServer;
bool gDnsServerStarted = false;
constexpr uint16_t kCaptiveDnsPort = 53;
bool gOtaInProgress = false;
bool gOtaHasError = false;
size_t gOtaBytesWritten = 0;
unsigned long gPendingRestartAtMs = 0;
char gOtaLastMessage[96] = "Idle";

static const char FY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>OinkSpy</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
html,body{height:100%;overflow:hidden}
body{font-family:'Courier New',monospace;background:#1f0f15;color:#fff1f5;display:flex;flex-direction:column}
.hd{background:#34131e;padding:10px 14px;border-bottom:2px solid #fb7185;flex-shrink:0}
.hd h1{font-size:22px;color:#fda4af;letter-spacing:2px;display:flex;align-items:center;justify-content:space-between;gap:12px}
.hd .sub{font-size:11px;color:#f9a8d4;margin-top:2px}
.pigwrap{display:flex;align-items:center;gap:8px}
.st{display:flex;gap:8px;padding:8px 12px;background:rgba(251,113,133,.08);border-bottom:1px solid rgba(251,113,133,.19);flex-shrink:0}
.sc{flex:1;text-align:center;padding:6px;border:1px solid rgba(251,113,133,.25);border-radius:5px}
.sc .n{font-size:22px;font-weight:bold;color:#fda4af}
.sc .l{font-size:10px;color:#fbcfe8;margin-top:2px}
.tb{display:flex;border-bottom:1px solid #fb7185;flex-shrink:0}
.tb button{flex:1;padding:9px;text-align:center;cursor:pointer;color:#f9a8d4;border:none;background:none;font-family:inherit;font-size:13px;font-weight:bold;letter-spacing:1px}
.tb button.a{color:#fff1f5;border-bottom:2px solid #fda4af;background:rgba(251,113,133,.08)}
.cn{flex:1;overflow-y:auto;padding:10px}
.pn{display:none}.pn.a{display:block}
.det{background:rgba(72,28,43,.55);border:1px solid rgba(251,113,133,.25);border-radius:7px;padding:10px;margin-bottom:8px}
.det .mac{color:#fda4af;font-weight:bold;font-size:14px}
.det .nm{color:#fbcfe8;font-size:13px;margin-left:4px}
.det .inf{display:flex;flex-wrap:wrap;gap:5px;margin-top:5px;font-size:12px}
.det .inf span{background:rgba(251,113,133,.15);padding:3px 6px;border-radius:4px}
.det .rv{background:rgba(239,68,68,.15)!important;color:#ef4444;font-weight:bold}
.pg{margin-bottom:12px}
.pg h3{color:#fda4af;font-size:14px;margin-bottom:4px;border-bottom:1px solid rgba(251,113,133,.19);padding-bottom:4px}
.pg .it{display:flex;flex-wrap:wrap;gap:4px;font-size:12px}
.pg .it span{background:rgba(251,113,133,.15);padding:3px 6px;border-radius:4px;border:1px solid rgba(251,113,133,.12)}
.btn{display:block;width:100%;padding:10px;margin-bottom:8px;background:#fb7185;color:#fff;border:none;border-radius:5px;cursor:pointer;font-family:inherit;font-size:14px;font-weight:bold}
.btn:active{background:#f472b6}
.btn.dng{background:#ef4444}
.empty{text-align:center;color:rgba(251,191,203,.55);padding:28px;font-size:14px}
.sep{border:none;border-top:1px solid rgba(251,113,133,.12);margin:12px 0}
h4{color:#fda4af;font-size:14px;margin-bottom:8px}
.pig{font-size:18px;color:#fbcfe8;transition:color .2s ease,transform .2s ease}
.pigm{font-size:10px;letter-spacing:1px;color:#f9a8d4;text-transform:uppercase}
.ev{background:rgba(72,28,43,.4);border:1px solid rgba(251,113,133,.18);border-radius:7px;padding:9px;margin-bottom:8px}
.ev .hdx{display:flex;justify-content:space-between;gap:8px;font-size:11px;color:#fbcfe8}
.ev .tp{font-weight:bold;color:#fda4af;letter-spacing:1px}
.ev .lb{display:inline-block;margin-top:6px;padding:3px 6px;border-radius:999px;background:rgba(251,113,133,.15);font-size:11px}
.ev .meta{display:flex;flex-wrap:wrap;gap:5px;margin-top:6px;font-size:11px}
.ev .meta span{background:rgba(251,113,133,.1);padding:3px 6px;border-radius:4px}
.guide{background:rgba(72,28,43,.42);border:1px solid rgba(251,113,133,.2);border-radius:8px;padding:10px;margin-bottom:10px}
.guide .ttl{font-size:11px;letter-spacing:1px;color:#fda4af;margin-bottom:6px}
.guide .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:6px}
.guide .it{background:rgba(251,113,133,.08);border:1px solid rgba(251,113,133,.14);border-radius:6px;padding:8px}
.guide .k{font-size:10px;color:#fbcfe8;letter-spacing:1px;margin-bottom:4px}
.guide .v{font-size:12px;line-height:1.35}
.guide .sys{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px;font-size:11px}
.guide .sys span{background:rgba(251,113,133,.1);padding:4px 6px;border-radius:999px}
.dbp{background:rgba(72,28,43,.42);border:1px solid rgba(251,113,133,.2);border-radius:8px;padding:10px;margin-bottom:10px}
.dbp h3{font-size:14px;color:#fda4af;margin-bottom:4px}
.dbp p{font-size:11px;color:#fbcfe8;line-height:1.4;margin-bottom:10px}
.dbg{background:rgba(251,113,133,.08);border:1px solid rgba(251,113,133,.14);border-radius:8px;padding:8px;margin-bottom:8px}
.dbgh{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:6px}
.dbgh label{font-size:12px;color:#fff1f5;font-weight:bold}
.dbgh input{accent-color:#fb7185}
.dbh{font-size:10px;color:#fbcfe8;margin-bottom:6px;line-height:1.4}
.dbta{width:100%;min-height:70px;background:rgba(31,15,21,.85);color:#fff1f5;border:1px solid rgba(251,113,133,.24);border-radius:6px;padding:8px;font-family:'Courier New',monospace;font-size:11px}
.dbta:focus{outline:none;border-color:#fda4af}
.dba{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.dba .btn{margin-bottom:0;flex:1;min-width:120px}
.dbhx{font-size:10px;color:#f9a8d4;margin-top:4px}
.launch{display:none;margin-top:10px;padding:10px;border:1px solid rgba(34,197,94,.28);border-radius:8px;background:rgba(20,83,45,.22)}
.launch.v{display:block}
.launch strong{display:block;font-size:12px;color:#dcfce7}
.launch span{display:block;margin-top:5px;font-size:11px;line-height:1.4;color:#bbf7d0}
.launch .rw{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.launch .rw .btn{margin-bottom:0;flex:1;min-width:150px}
</style></head><body>
<div class="hd"><h1><span>OINKSPY</span><span class="pigwrap"><span class="pigm" id="pigMode">IDLE</span><span class="pig" id="pigFace">(^oo^)</span></span></h1><div class="sub">Pig-themed surveillance detector &bull; wardriving + GPS</div></div>
<div class="st">
<div class="sc"><div class="n" id="sT">0</div><div class="l">DETECTED</div></div>
<div class="sc"><div class="n" id="sR">0</div><div class="l">RAVEN</div></div>
<div class="sc"><div class="n" id="sB">ON</div><div class="l">BLE</div></div>
<div class="sc" onclick="reqGPS()" style="cursor:pointer"><div class="n" id="sG" style="font-size:14px">TAP</div><div class="l">GPS</div></div>
</div>
<div class="tb">
<button class="a" onclick="tab(0,this)">LIVE</button>
<button onclick="tab(1,this)">PREV</button>
<button onclick="tab(2,this)">DB</button>
<button onclick="tab(3,this)">TOOLS</button>
</div>
<div class="cn">
<div class="pn a" id="p0">
<div class="guide">
<div class="ttl">FIELD CHECKLIST</div>
<div class="grid">
<div class="it"><div class="k">CONNECT</div><div class="v">Join the OinkSpy AP and keep this page open while the detector scans.</div></div>
<div class="it"><div class="k">GPS</div><div class="v" id="gHint">Tap GPS for browser tagging, or use Grove GNSS on D6/D7 for on-device fixes.</div></div>
<div class="it"><div class="k">EXPORT</div><div class="v">Open Tools to download JSON, CSV, or KML once detections start rolling in.</div></div>
</div>
<div class="sys">
<span id="sysClock">Time: checking</span>
<span id="sysStore">Storage: checking</span>
<span id="sysGnss">GNSS: checking</span>
<span id="sysRtc">RTC: checking</span>
<span id="sysWardrive">Wardrive: checking</span>
</div>
<div class="launch" id="launchGuide"><strong id="launchTitle">Open the full browser for GPS</strong><span id="launchText">If this page opened inside a Wi-Fi sign-in helper, location is more reliable after opening the dashboard in your phone browser.</span><div class="rw"><button class="btn" onclick="openFullBrowser()" style="background:#22c55e">OPEN IN BROWSER</button><button class="btn" onclick="dismissLaunchGuide(true)" style="background:#6366f1">CONTINUE HERE</button></div></div>
</div>
<div id="dL"><div class="empty">Snout up, scanning for surveillance gear...<br>BLE active on all channels</div></div>
</div>
<div class="pn" id="p1"><div id="hL"><div class="empty">Loading prior session...</div></div></div>
<div class="pn" id="p2"><div id="pC">Loading patterns...</div></div>
<div class="pn" id="p3">
<h4>WARDRIVE MODE</h4>
<p style="font-size:10px;color:#f9a8d4;margin-bottom:8px">Continuously write deduplicated detection rows to SD card CSV while scanning is active.</p>
<div class="dbg">
<div class="dbgh"><label>Wardrive Mode</label><label><input id="wardriveToggle" type="checkbox" onchange="setWardriveEnabled(this.checked)"> enabled</label></div>
<div class="dbh" id="wardriveStatus">Wardrive: checking</div>
</div>
<hr class="sep">
<h4>EXPORT DETECTIONS</h4>
<p style="font-size:10px;color:#f9a8d4;margin-bottom:8px">Download the current session for desktop review, mapping, or archive</p>
<button class="btn" onclick="location.href='/api/export/json'">DOWNLOAD JSON</button>
<button class="btn" onclick="location.href='/api/export/csv'">DOWNLOAD CSV</button>
<button class="btn" onclick="location.href='/api/export/kml'" style="background:#22c55e">DOWNLOAD KML (GPS MAP)</button>
<button class="btn" onclick="location.href='/api/export/wigle'" style="background:#0ea5e9">DOWNLOAD WIGLE CSV</button>
<hr class="sep">
<h4>CLIENT WIFI</h4>
<p style="font-size:10px;color:#f9a8d4;margin-bottom:8px">Store uplink Wi-Fi credentials in device memory only. If supplied on SD once, OinkSpy imports and scrubs them from the SD config.</p>
<div class="dbg">
<input id="clientWifiSsid" type="text" placeholder="Client Wi-Fi SSID" style="width:100%;padding:8px;background:rgba(31,15,21,.85);color:#fff1f5;border:1px solid rgba(251,113,133,.24);border-radius:6px;margin-bottom:8px">
<input id="clientWifiPassword" type="password" placeholder="Client Wi-Fi password" style="width:100%;padding:8px;background:rgba(31,15,21,.85);color:#fff1f5;border:1px solid rgba(251,113,133,.24);border-radius:6px">
<div class="dbh" id="clientWifiStatus" style="margin-top:8px">Client Wi-Fi: checking</div>
</div>
<div class="rw">
<button class="btn" onclick="saveClientWifi()" style="background:#0ea5e9">SAVE CLIENT WIFI</button>
<button class="btn dng" onclick="clearClientWifi()">CLEAR CLIENT WIFI</button>
</div>
<hr class="sep">
<h4>WIGLE</h4>
<p style="font-size:10px;color:#f9a8d4;margin-bottom:8px">Store WiGLE API credentials in memory only, then upload the latest closed WiGLE CSV captured from GPS-tagged Wi-Fi AP scans.</p>
<div class="dbg">
<input id="wigleApiName" type="text" placeholder="WiGLE API name" style="width:100%;padding:8px;background:rgba(31,15,21,.85);color:#fff1f5;border:1px solid rgba(251,113,133,.24);border-radius:6px;margin-bottom:8px">
<input id="wigleApiToken" type="password" placeholder="WiGLE API token" style="width:100%;padding:8px;background:rgba(31,15,21,.85);color:#fff1f5;border:1px solid rgba(251,113,133,.24);border-radius:6px">
<div class="dbh" id="wigleStatus" style="margin-top:8px">WiGLE: checking</div>
</div>
<div class="rw">
<button class="btn" onclick="saveWigleCreds()" style="background:#0ea5e9">SAVE WIGLE CREDS</button>
<button class="btn" onclick="uploadWigle()" style="background:#22c55e">UPLOAD TO WIGLE</button>
<button class="btn dng" onclick="clearWigleCreds()">CLEAR WIGLE CREDS</button>
</div>
<hr class="sep">
<h4>PRIOR SESSION</h4>
<button class="btn" onclick="location.href='/api/history/json'" style="background:#6366f1">DOWNLOAD PREV JSON</button>
<button class="btn" onclick="location.href='/api/history/kml'" style="background:#22c55e">DOWNLOAD PREV KML</button>
<hr class="sep">
<h4>FIRMWARE UPDATE</h4>
<p style="font-size:10px;color:#f9a8d4;margin-bottom:8px">Upload a `.bin` built for the XIAO ESP32-S3. The device reboots after a successful flash.</p>
<div class="dbg">
<div class="dbh">Use the PlatformIO `firmware.bin` artifact and keep power stable until the update completes.</div>
<input id="otaFile" type="file" accept=".bin,application/octet-stream" style="width:100%;padding:8px;background:rgba(31,15,21,.85);color:#fff1f5;border:1px solid rgba(251,113,133,.24);border-radius:6px">
<div class="dbh" id="otaStatus" style="margin-top:8px">OTA: checking</div>
</div>
<button class="btn" onclick="uploadOta()" style="background:#f97316">UPLOAD OTA BIN</button>
<hr class="sep">
<h4>RECENT EVENTS</h4>
<p style="font-size:10px;color:#f9a8d4;margin-bottom:8px">Latest bookmarks and detections recorded by the device</p>
<div id="eL"><div class="empty">No recent events yet</div></div>
<hr class="sep">
<button class="btn dng" onclick="if(confirm('Clear all detections?'))fetch('/api/clear').then(()=>refresh())">CLEAR ALL DETECTIONS</button>
</div>
</div>
<script>
let D=[],H=[],E=[];
let P={scanEnabled:true,wardriveEnabled:false,wardriveActive:false,sdReady:true,sdHealthy:true,ota:false};
const PATTERN_GROUPS=[{key:'macs',label:'Oink MAC Prefixes',hint:'One MAC prefix per line, formatted like 58:8e:81.'},{key:'macs_mfr',label:'Contract Mfr MACs',hint:'Lower-confidence manufacturer MAC prefixes, one per line.'},{key:'macs_soundthinking',label:'SoundThinking MACs',hint:'Prefixes matched before manufacturer fallbacks.'},{key:'names',label:'BLE Device Names',hint:'Case-insensitive substrings, one pattern per line.'},{key:'mfr',label:'BLE Manufacturer IDs',hint:'Hex IDs like 0x09C8, one per line.'},{key:'raven',label:'Raven UUIDs',hint:'Full service UUIDs, one per line.'}];
function setTxt(id,text,color){let el=document.getElementById(id);if(!el)return;el.textContent=text;if(color)el.style.color=color;}
function setPig(face,label,color,title){let pig=document.getElementById('pigFace');let mode=document.getElementById('pigMode');if(pig){pig.textContent=face;pig.style.color=color||'#fbcfe8';pig.title=title||'';} if(mode){mode.textContent=label||'IDLE';mode.style.color=color||'#f9a8d4';}}
function updatePig(){if(P.ota){setPig('(^@@^)', 'UPDATE', '#f97316', 'Firmware update in progress');return;} if((P.wardriveEnabled&&!P.sdReady)||!P.sdHealthy){setPig('(xoox)', 'ERROR', '#ef4444', 'Storage or logging issue needs attention');return;} if(!P.scanEnabled){setPig('(-.-)', 'PAUSED', '#facc15', 'Scanning is paused');return;} if(P.wardriveActive){setPig('(^OO^)', 'WDRIVE', '#22c55e', 'Wardrive mode actively logging scans');return;} setPig('(-oo-)', 'FLOCK', '#60a5fa', 'Flock drive is scanning for targeted surveillance gear');}
function tab(i,el){document.querySelectorAll('.tb button').forEach(b=>b.classList.remove('a'));document.querySelectorAll('.pn').forEach(p=>p.classList.remove('a'));el.classList.add('a');document.getElementById('p'+i).classList.add('a');if(i===1&&!window._hL)loadHistory();if(i===2&&!window._pL)loadPat();if(i===3)loadEvents();}
function refresh(){fetch('/api/detections').then(r=>r.json()).then(d=>{D=d;render();stats();}).catch(()=>{document.getElementById('dL').innerHTML='<div class="empty">Live detections unavailable right now.<br>Stay connected to the OinkSpy AP and refresh in a moment.</div>';});}
function render(){const el=document.getElementById('dL');if(!D.length){el.innerHTML='<div class="empty">Snout up, scanning for surveillance gear...<br>BLE matches and passive Wi-Fi AP scans will appear here live</div>';return;} D.sort((a,b)=>b.last-a.last);el.innerHTML=D.map(card).join('');}
function portalUrl(){return window.location.origin+'/';}
function dismissLaunchGuide(persist){let el=document.getElementById('launchGuide');if(el)el.classList.remove('v');if(persist){try{localStorage.setItem('oink_launch_guide_dismissed','1');}catch(err){}}}
function showLaunchGuide(title,body){let el=document.getElementById('launchGuide');if(!el)return;document.getElementById('launchTitle').textContent=title;document.getElementById('launchText').textContent=body;el.classList.add('v');}
function openFullBrowser(){dismissLaunchGuide(true);let url=portalUrl();try{let win=window.open(url,'_blank','noopener,noreferrer');if(win)return;}catch(err){}window.location.href=url;}
function maybeShowLaunchGuide(){let dismissed=false;try{dismissed=localStorage.getItem('oink_launch_guide_dismissed')==='1';}catch(err){}if(dismissed)return;if(!window.isSecureContext){setTxt('gHint','This page may be running in a captive-portal helper. Open the full browser for the best GPS chance.','#facc15');showLaunchGuide('Open the full browser for GPS','This page likely opened inside a Wi-Fi sign-in helper. Location permissions are more reliable after opening the dashboard in your normal phone browser.');return;} if(navigator.geolocation&&(!navigator.permissions||!navigator.permissions.query)){showLaunchGuide('GPS controls are limited here','If GPS does not start after tapping the badge, open the dashboard in your normal phone browser and try again.');}}
function stats(){fetch('/api/stats').then(r=>r.json()).then(s=>{document.getElementById('sT').textContent=s.total;document.getElementById('sR').textContent=s.raven;P.scanEnabled=!!s.scan_enabled;P.sdReady=!!s.sd_ready;P.sdHealthy=!!s.sd_logging_healthy;updatePig();}).catch(()=>{});}
function methodLabel(m){return({wifi_ap:'Wi-Fi AP',mac_prefix:'MAC prefix',mac_prefix_soundthinking:'SoundThinking MAC',mac_prefix_mfr:'Mfr MAC',device_name:'Device name',ble_mfr_id:'BLE mfr ID',raven_uuid:'Raven UUID'})[m]||m;}
function card(d){return '<div class="det"><div class="mac">'+d.mac+(d.name?'<span class="nm">'+d.name+'</span>':'')+'</div><div class="inf"><span>RSSI: '+d.rssi+'</span><span>'+methodLabel(d.method)+'</span><span style="color:#fda4af;font-weight:bold">&times;'+d.count+'</span>'+(d.raven?'<span class="rv">RAVEN '+d.fw+'</span>':'')+(d.gps?'<span style="color:#22c55e">&#9673; '+d.gps.lat.toFixed(5)+','+d.gps.lon.toFixed(5)+'</span>':'<span style="color:#666">no gps</span>')+'</div></div>';}
function eventCard(e){let title=e.record_type==='bookmark'?'BOOKMARK':'DETECTION';let label=e.label?'<div class="lb">'+e.label+'</div>':'';let gps=e.gps?'<span>GPS '+e.gps.lat.toFixed(5)+','+e.gps.lon.toFixed(5)+'</span>':'';let who=e.mac?'<span>'+e.mac+(e.name?' '+e.name:'')+'</span>':'';let sig=e.record_type==='detection'?'<span>RSSI '+e.rssi+'</span><span>'+e.method+'</span><span>&times;'+e.count+'</span>':'';let rv=e.is_raven?'<span>RAVEN '+e.raven_fw+'</span>':'';return '<div class="ev"><div class="hdx"><span class="tp">'+title+'</span><span>'+(e.iso8601||('ms '+e.millis))+'</span></div>'+label+'<div class="meta"><span>boot '+e.boot_count+'</span><span>'+e.time_source+'</span>'+who+sig+rv+gps+'</div></div>';}
function loadHistory(){fetch('/api/history').then(r=>r.json()).then(d=>{H=d;let el=document.getElementById('hL');if(!H.length){el.innerHTML='<div class="empty">No prior session data</div>';return;} H.sort((a,b)=>b.last-a.last);el.innerHTML='<div style="font-size:11px;color:#f9a8d4;margin-bottom:8px">'+H.length+' detections from prior session</div>'+H.map(card).join('');window._hL=1;}).catch(()=>{document.getElementById('hL').innerHTML='<div class="empty">No prior session data</div>';});}
function esc(v){return String(v||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
function patternAreaValue(values){return (values||[]).join('\n');}
function renderPatternGroup(def,p){let group=p[def.key]||{enabled:true,values:[]};return '<div class="dbg"><div class="dbgh"><label>'+def.label+'</label><label><input type="checkbox" id="pat_'+def.key+'_enabled" '+(group.enabled?'checked':'')+'> enabled</label></div><div class="dbh">'+def.hint+'</div><textarea class="dbta" id="pat_'+def.key+'_values">'+esc(patternAreaValue(group.values))+'</textarea></div>';}
function renderPatternEditor(p){let h='<div class="dbp"><h3>Detection Parameters</h3><p>Toggle each rule family on or off, then edit one value per line. Changes save to the device and take effect immediately.</p>';h+=PATTERN_GROUPS.map(def=>renderPatternGroup(def,p)).join('');h+='<div class="dba"><button class="btn" onclick="savePat(false)">SAVE PROFILE</button><button class="btn" onclick="loadPat()" style="background:#6366f1">RELOAD</button><button class="btn dng" onclick="savePat(true)">RESET DEFAULTS</button></div><div class="dbhx">MAC prefixes use the first 3 bytes only. BLE manufacturer IDs expect hex, and Raven UUIDs should stay full-length.</div></div>';document.getElementById('pC').innerHTML=h;}
function patternLines(key){let raw=(document.getElementById('pat_'+key+'_values')||{}).value||'';return raw.split(/\r?\n|,/).map(v=>v.trim()).filter(Boolean);}
function patternPayload(reset){if(reset){return {reset_defaults:true};} let payload={}; PATTERN_GROUPS.forEach(def=>{payload[def.key]={enabled:!!document.getElementById('pat_'+def.key+'_enabled').checked,values:patternLines(def.key)};}); return payload;}
function loadPat(){fetch('/api/patterns').then(r=>r.json()).then(p=>{renderPatternEditor(p);window._pL=1;}).catch(()=>{document.getElementById('pC').innerHTML='<div class="empty">Detection database unavailable</div>';});}
function savePat(reset){fetch('/api/patterns',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(patternPayload(reset))}).then(r=>r.json()).then(p=>{if(p.status&&p.status!=='success'){throw new Error(p.message||'save failed');} loadPat(); alert(reset?'Detection parameters reset to defaults.':'Detection parameters saved.');}).catch(err=>{alert('Detection parameter update failed: '+err.message);});}
function loadEvents(){fetch('/api/events').then(r=>r.json()).then(d=>{E=d;let el=document.getElementById('eL');if(!E.length){el.innerHTML='<div class="empty">No recent events yet</div>';return;} el.innerHTML=E.map(eventCard).join('');}).catch(()=>{document.getElementById('eL').innerHTML='<div class="empty">Recent events unavailable</div>';});}
function loadWardriveStatus(){fetch('/api/wardrive').then(r=>r.json()).then(w=>{P.wardriveEnabled=!!w.enabled;P.wardriveActive=!!w.active;P.sdReady=!!w.sd_ready;updatePig();let toggle=document.getElementById('wardriveToggle');if(toggle)toggle.checked=!!w.enabled;let txt='Wardrive: '+(w.active?'logging to '+(w.current_path||'SD'):(w.message||'idle'));let color=w.active?'#22c55e':w.enabled?'#facc15':'#f9a8d4';setTxt('wardriveStatus',txt,color);setTxt('sysWardrive','Wardrive: '+(w.active?'ON':w.enabled?'ARMED':'OFF'),color);}).catch(()=>{setTxt('wardriveStatus','Wardrive: unavailable','#ef4444');setTxt('sysWardrive','Wardrive: unavailable','#ef4444');});}
function setWardriveEnabled(enabled){fetch('/api/wardrive',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:!!enabled})}).then(async r=>{let data=await r.json().catch(()=>({}));if(!r.ok||data.status==='error'){throw new Error(data.message||'Wardrive update failed');} loadWardriveStatus(); if(data.message&&data.message!=='ok'){alert(data.message);} }).catch(err=>{let toggle=document.getElementById('wardriveToggle');if(toggle)toggle.checked=!enabled;loadWardriveStatus();alert('Wardrive update failed: '+err.message);});}
function loadClientWifiStatus(){fetch('/api/client-wifi').then(r=>r.json()).then(s=>{setTxt('clientWifiStatus','Client Wi-Fi: '+(s.connected?('connected to '+(s.ssid||'network')+' '+(s.ip||'')):s.status||'idle'),s.connected?'#22c55e':s.configured?'#facc15':'#f9a8d4');let ssid=document.getElementById('clientWifiSsid');if(ssid&&!ssid.value&&s.configured)ssid.placeholder=s.ssid||'stored';}).catch(()=>{setTxt('clientWifiStatus','Client Wi-Fi: unavailable','#ef4444');});}
function saveClientWifi(){let ssid=(document.getElementById('clientWifiSsid')||{}).value||'';let password=(document.getElementById('clientWifiPassword')||{}).value||'';fetch('/api/client-wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:password})}).then(async r=>{let data=await r.json().catch(()=>({}));if(!r.ok||data.status==='error')throw new Error(data.message||'Client Wi-Fi save failed');loadClientWifiStatus();alert(data.message||'Client Wi-Fi stored.');}).catch(err=>alert('Client Wi-Fi update failed: '+err.message));}
function clearClientWifi(){fetch('/api/client-wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({forget:true})}).then(async r=>{let data=await r.json().catch(()=>({}));if(!r.ok||data.status==='error')throw new Error(data.message||'Client Wi-Fi clear failed');let ssid=document.getElementById('clientWifiSsid');let pass=document.getElementById('clientWifiPassword');if(ssid)ssid.value='';if(pass)pass.value='';loadClientWifiStatus();}).catch(err=>alert('Client Wi-Fi clear failed: '+err.message));}
function loadWigleStatus(){fetch('/api/wigle').then(r=>r.json()).then(s=>{let txt='WiGLE: '+(s.upload_in_progress?'uploading':s.last_status||'idle');if(s.current_path)txt+=' | file '+s.current_path;if(!s.upload_in_progress&&s.blocked_reason)txt+=' | '+s.blocked_reason;setTxt('wigleStatus',txt,s.upload_in_progress?'#facc15':s.ready?'#22c55e':s.configured?'#f59e0b':'#f9a8d4');let name=document.getElementById('wigleApiName');if(name&&!name.value&&s.api_name_hint)name.placeholder=s.api_name_hint;}).catch(()=>{setTxt('wigleStatus','WiGLE: unavailable','#ef4444');});}
function saveWigleCreds(){let apiName=(document.getElementById('wigleApiName')||{}).value||'';let apiToken=(document.getElementById('wigleApiToken')||{}).value||'';fetch('/api/wigle',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({api_name:apiName,api_token:apiToken})}).then(async r=>{let data=await r.json().catch(()=>({}));if(!r.ok||data.status==='error')throw new Error(data.message||'WiGLE save failed');loadWigleStatus();alert(data.message||'WiGLE credentials stored.');}).catch(err=>alert('WiGLE update failed: '+err.message));}
function clearWigleCreds(){fetch('/api/wigle',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({forget:true})}).then(async r=>{let data=await r.json().catch(()=>({}));if(!r.ok||data.status==='error')throw new Error(data.message||'WiGLE clear failed');let name=document.getElementById('wigleApiName');let token=document.getElementById('wigleApiToken');if(name)name.value='';if(token)token.value='';loadWigleStatus();}).catch(err=>alert('WiGLE clear failed: '+err.message));}
function uploadWigle(){fetch('/api/wigle/upload',{method:'POST'}).then(async r=>{let data=await r.json().catch(()=>({}));if(!r.ok||data.status==='error')throw new Error(data.message||'WiGLE upload failed');loadWigleStatus();alert(data.message||'WiGLE upload queued.');}).catch(err=>alert('WiGLE upload failed: '+err.message));}
function loadOtaStatus(){fetch('/api/ota/status').then(r=>r.json()).then(s=>{P.ota=!!s.in_progress;updatePig();let txt=!s.enabled?'OTA: disabled in config':s.in_progress?'OTA: writing '+s.bytes_written+' bytes':s.pending_reboot?'OTA: update applied, reboot pending':'OTA: '+(s.message||'idle');setTxt('otaStatus',txt,s.error?'#ef4444':s.pending_reboot?'#22c55e':'#f9a8d4');}).catch(()=>{setTxt('otaStatus','OTA: unavailable','#ef4444');});}
function uploadOta(){let fileInput=document.getElementById('otaFile');if(!fileInput||!fileInput.files||!fileInput.files.length){alert('Choose a firmware .bin file first.');return;} let fd=new FormData();fd.append('firmware',fileInput.files[0]);setTxt('otaStatus','OTA: uploading...','#facc15');fetch('/api/ota',{method:'POST',body:fd}).then(async r=>{let data=await r.json().catch(()=>({}));if(!r.ok||data.status!=='success'){throw new Error(data.message||'OTA upload failed');} setTxt('otaStatus','OTA: '+(data.message||'success'),'#22c55e'); alert(data.message||'Firmware updated. The device will reboot shortly.');}).catch(err=>{setTxt('otaStatus','OTA: '+err.message,'#ef4444'); alert('OTA upload failed: '+err.message);}).finally(()=>loadOtaStatus());}
function satLabel(v){return v>=0?String(v):'-';}
function setBrowserGpsHint(text,color){setTxt('gHint',text,color||'#f9a8d4');}
function loadPortalMeta(){fetch('/api/time').then(r=>r.json()).then(t=>{setTxt('sysClock',t.synced?'Time: '+t.time_source+' '+t.iso8601:'Time: waiting for sync',t.synced?'#22c55e':'#facc15');}).catch(()=>{setTxt('sysClock','Time: unavailable','#ef4444');});fetch('/api/storage').then(r=>r.json()).then(s=>{P.sdReady=!!s.sd_ready;let txt=s.sd_ready?'Storage: SD ready':'Storage: SD missing';if(s.log_events_dropped>0)txt+=' drops:'+s.log_events_dropped;setTxt('sysStore',txt,s.sd_ready&&s.sd_logging_enabled?'#22c55e':'#f59e0b');updatePig();}).catch(()=>{setTxt('sysStore','Storage: unavailable','#ef4444');});fetch('/api/gnss').then(r=>r.json()).then(g=>{let badge=satLabel(g.satellites_seen)+'/'+satLabel(g.satellites_used);let color=g.has_fix?'#22c55e':g.gps_seen?'#facc15':'#f9a8d4';setGPSBadge(g.enabled?badge:'OFF',g.enabled?color:'#ef4444',g.enabled?'Satellites seen / used':'GNSS disabled');let txt=!g.enabled?'GNSS: off':g.gps_seen?('GNSS: sats '+badge+(g.has_fix?' fix':' no fix')):'GNSS: waiting on D6/D7';setTxt('sysGnss',txt,color);}).catch(()=>{setGPSBadge('ERR','#ef4444','GNSS unavailable');setTxt('sysGnss','GNSS: unavailable','#ef4444');});fetch('/api/rtc').then(r=>r.json()).then(rt=>{let txt=!rt.enabled?'RTC: off':!rt.present?'RTC: not found':rt.time_valid?'RTC: '+rt.iso8601:'RTC: found, time unset';setTxt('sysRtc',txt,rt.time_valid?'#22c55e':rt.present?'#facc15':'#f9a8d4');}).catch(()=>{setTxt('sysRtc','RTC: unavailable','#ef4444');});loadWardriveStatus();loadClientWifiStatus();loadWigleStatus();loadOtaStatus();}
let _gW=null,_gOk=false,_gTried=false,_gPerm='unknown',_gPermStatus=null;
function setGPSBadge(text,color,title){let g=document.getElementById('sG');g.textContent=text;g.style.color=color||'#fda4af';g.title=title||'';}
function stopGPS(){if(_gW!==null&&navigator.geolocation){navigator.geolocation.clearWatch(_gW);} _gW=null;}
function sendGPS(p){_gOk=true;setBrowserGpsHint('Browser GPS tagging active. Grove GNSS badge shows satellites seen/used.','#22c55e'); fetch('/api/gps?lat='+p.coords.latitude+'&lon='+p.coords.longitude+'&acc='+(p.coords.accuracy||0)).catch(()=>{});}
function gpsErr(e){_gOk=false;if(e&&e.code===1){stopGPS();_gPerm='denied';setBrowserGpsHint('Browser GPS permission denied. GNSS badge still reflects D6/D7 satellite data.','#ef4444');alert('GPS permission denied. On iPhone, GPS requires HTTPS which this device cannot provide. On Android Chrome, tap the lock/info icon in the address bar and allow Location.');return;} if(e&&e.code===2){setBrowserGpsHint('Browser GPS unavailable right now. GNSS badge still reflects device satellite data.','#ef4444');return;} if(e&&e.code===3){setBrowserGpsHint('Waiting for browser GPS fix. GNSS badge shows satellites seen/used.','#facc15');return;} setBrowserGpsHint('Browser GPS error. GNSS badge still reflects device satellite data.','#ef4444');}
function startGPS(autoStart){if(!navigator.geolocation){return false;} if(_gW!==null){return true;} setBrowserGpsHint(autoStart?'Browser GPS autostarting for tagging.':'Requesting browser GPS for tagging.','#facc15'); _gW=navigator.geolocation.watchPosition(sendGPS,gpsErr,{enableHighAccuracy:true,maximumAge:5000,timeout:15000}); return true;}
function applyGPSPermission(state,autoStart){_gPerm=state;if(state==='granted'){if(_gOk){setBrowserGpsHint('Browser GPS tagging active. GNSS badge shows satellites seen/used.','#22c55e');return;} if(autoStart){startGPS(true);} else {setBrowserGpsHint('Browser location permission granted. Tap GPS to start tagging.','#22c55e');} return;} if(state==='prompt'){_gOk=false;stopGPS();setBrowserGpsHint(_gTried?'Waiting for browser GPS permission or fix.':'Tap GPS to enable browser tagging.','#facc15');return;} if(state==='denied'){_gOk=false;stopGPS();setBrowserGpsHint('Browser GPS blocked in site settings. GNSS badge still shows device satellites.','#ef4444');return;} setBrowserGpsHint('Tap GPS to start browser location tagging.','#facc15');}
async function bootGPS(){if(!navigator.geolocation){setBrowserGpsHint('Browser GPS not available here. Use Grove GNSS on D6/D7 for on-device fixes.','#ef4444');return;} if(!window.isSecureContext){setBrowserGpsHint('Open the full browser or allow insecure-origin GPS if you want browser tagging.','#f59e0b');return;} if(!navigator.permissions||!navigator.permissions.query){setBrowserGpsHint('Tap GPS to request browser location access for tagging.','#facc15');return;} try{_gPermStatus=await navigator.permissions.query({name:'geolocation'});applyGPSPermission(_gPermStatus.state,true);let onChange=()=>applyGPSPermission(_gPermStatus.state,true);if(typeof _gPermStatus.addEventListener==='function'){_gPermStatus.addEventListener('change',onChange);} else {_gPermStatus.onchange=onChange;}}catch(err){console.log('Geolocation permission query failed:',err);setBrowserGpsHint('Tap GPS to request browser location access for tagging.','#facc15');}}
function reqGPS(){if(!navigator.geolocation){alert('GPS not available in this browser.');return;} _gTried=true; if(!window.isSecureContext){setBrowserGpsHint('Browser GPS requires HTTPS or the Android insecure-origin override.','#f59e0b');alert('GPS requires a secure context (HTTPS). This HTTP page may not get GPS permission.\n\nAndroid Chrome: try chrome://flags and enable "Insecure origins treated as secure", add http://192.168.4.1\n\niPhone: GPS will not work over HTTP.');return;} if(_gPerm==='denied'){setBrowserGpsHint('Browser GPS is blocked in site settings.','#ef4444');alert('GPS access is blocked in browser settings. Re-enable Location for this site and reload the dashboard.');return;} startGPS(false);}
function syncClock(){fetch('/api/time?epoch='+Math.floor(Date.now()/1000)).catch(()=>{});}
refresh();loadEvents();loadPortalMeta();syncClock();maybeShowLaunchGuide();bootGPS();setInterval(refresh,2500);setInterval(loadEvents,3000);setInterval(loadPortalMeta,5000);setInterval(syncClock,60000);
</script></body></html>
)rawliteral";

String portalBaseUrl() {
    return String("http://") + WiFi.softAPIP().toString() + "/";
}

void addCommonHeaders(AsyncWebServerResponse* response) {
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("Permissions-Policy", "geolocation=(self)");
    response->addHeader("X-Content-Type-Options", "nosniff");
}

void sendPortalPage(AsyncWebServerRequest* request, int code = 200) {
    AsyncWebServerResponse* response =
        request->beginResponse(code, "text/html; charset=utf-8", FY_HTML);
    addCommonHeaders(response);
    response->addHeader("X-Captive-Portal", "true");
    request->send(response);
}

void sendPortalRedirect(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(302);
    addCommonHeaders(response);
    response->addHeader("Location", portalBaseUrl());
    request->send(response);
}

void handlePortalProbe(AsyncWebServerRequest* request) {
    printf("[OINK-YOU] Captive portal probe: %s\n", request->url().c_str());
    sendPortalRedirect(request);
}

void setOtaMessage(const char* message) {
    strlcpy(gOtaLastMessage, message ? message : "Idle", sizeof(gOtaLastMessage));
}

void setOtaError(const char* message) {
    gOtaHasError = true;
    setOtaMessage(message && message[0] ? message : "Update failed");
}

void writeOtaStatusJson(Print& out) {
    out.printf("{\"enabled\":%s,\"in_progress\":%s,\"error\":%s,\"bytes_written\":%u,\"pending_reboot\":%s,\"message\":\"%s\"}",
               gApp.runtimeConfig.otaEnabled ? "true" : "false",
               gOtaInProgress ? "true" : "false",
               gOtaHasError ? "true" : "false",
               static_cast<unsigned>(gOtaBytesWritten),
               gPendingRestartAtMs != 0 ? "true" : "false",
               gOtaLastMessage);
}

void beginCaptivePortalDns() {
    gDnsServer.stop();
    gDnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    gDnsServer.start(kCaptiveDnsPort, "*", WiFi.softAPIP());
    gDnsServerStarted = true;
    printf("[OINK-YOU] Captive DNS: * -> %s\n", WiFi.softAPIP().toString().c_str());
}

void writeCsv(AsyncResponseStream* resp) {
    resp->println("mac,name,rssi,method,first_seen_ms,last_seen_ms,count,is_raven,raven_fw,latitude,longitude,gps_accuracy");
    if (gApp.mutex && xSemaphoreTake(gApp.mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (int i = 0; i < gApp.detectionCount; ++i) {
            Detection& detection = gApp.detections[i];
            if (detection.hasGPS) {
                resp->printf("\"%s\",\"%s\",%d,\"%s\",%lu,%lu,%d,%s,\"%s\",%.8f,%.8f,%.1f\n",
                             detection.mac,
                             detection.name,
                             detection.rssi,
                             detection.method,
                             detection.firstSeen,
                             detection.lastSeen,
                             detection.count,
                             detection.isRaven ? "true" : "false",
                             detection.ravenFW,
                             detection.gpsLat,
                             detection.gpsLon,
                             detection.gpsAcc);
            } else {
                resp->printf("\"%s\",\"%s\",%d,\"%s\",%lu,%lu,%d,%s,\"%s\",,,\n",
                             detection.mac,
                             detection.name,
                             detection.rssi,
                             detection.method,
                             detection.firstSeen,
                             detection.lastSeen,
                             detection.count,
                             detection.isRaven ? "true" : "false",
                             detection.ravenFW);
            }
        }
        xSemaphoreGive(gApp.mutex);
    }
}

void setupServer() {
    gServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendPortalPage(request);
    });

    gServer.on("/generate_204", HTTP_ANY, handlePortalProbe);
    gServer.on("/gen_204", HTTP_ANY, handlePortalProbe);
    gServer.on("/hotspot-detect.html", HTTP_ANY, handlePortalProbe);
    gServer.on("/canonical.html", HTTP_ANY, handlePortalProbe);
    gServer.on("/success.txt", HTTP_ANY, handlePortalProbe);
    gServer.on("/ncsi.txt", HTTP_ANY, handlePortalProbe);
    gServer.on("/connecttest.txt", HTTP_ANY, handlePortalProbe);
    gServer.on("/redirect", HTTP_ANY, handlePortalProbe);
    gServer.on("/fwlink", HTTP_ANY, handlePortalProbe);
    gServer.on("/mobile/status.php", HTTP_ANY, handlePortalProbe);
    gServer.on("/library/test/success.html", HTTP_ANY, handlePortalProbe);

    gServer.on("/api/detections", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::log::writeDetectionsJson(resp);
        request->send(resp);
    });
    gServer.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::log::writeRecentEventsJson(resp);
        request->send(resp);
    });

    gServer.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest* request) {
        char buf[320];
        snprintf(buf,
                 sizeof(buf),
                 "{\"total\":%lu,\"live_total\":%d,\"raven\":%d,\"ble\":\"active\",\"gps_valid\":%s,\"gps_age\":%lu,\"gps_tagged\":%lu,\"gps_tagged_live\":%d,\"sd_ready\":%s,\"sd_logging_healthy\":%s,\"time_synced\":%s,\"time_source\":\"%s\",\"day_token\":\"%s\",\"scan_enabled\":%s,\"companion_active\":%s}",
                 gApp.sessionDiscoveryCount,
                 gApp.detectionCount,
                 oink::scan::countRavenDetections(),
                 oink::scan::gpsIsFresh() ? "true" : "false",
                 gApp.gpsValid ? (millis() - gApp.gpsLastUpdate) : 0UL,
                 gApp.sessionGpsTaggedCount,
                 oink::scan::countGpsTaggedDetections(),
                 gApp.sdReady ? "true" : "false",
                 gApp.sdLoggingHealthy ? "true" : "false",
                 oink::timeutil::isSynced() ? "true" : "false",
                 oink::timeutil::timeSourceLabel(),
                 gApp.dayToken,
                 oink::scan::isScanningEnabled() ? "true" : "false",
                 (gApp.bleClientConnected || gApp.serialHostConnected) ? "true" : "false");
        request->send(200, "application/json", buf);
    });

    gServer.on("/api/storage", HTTP_GET, [](AsyncWebServerRequest* request) {
        char buf[768];
        snprintf(buf,
                 sizeof(buf),
                 "{\"spiffs_ready\":%s,\"sd_ready\":%s,\"sd_logging_enabled\":%s,\"log_worker_ready\":%s,\"log_queue_depth\":%u,\"log_events_written\":%lu,\"log_events_dropped\":%lu,\"ota_enabled\":%s,\"session_csv\":\"%s\",\"session_jsonl\":\"%s\",\"daily_csv\":\"%s\",\"daily_jsonl\":\"%s\"}",
                 gApp.spiffsReady ? "true" : "false",
                 gApp.sdReady ? "true" : "false",
                 gApp.runtimeConfig.sdLoggingEnabled ? "true" : "false",
                 gApp.logWorkerReady ? "true" : "false",
                 static_cast<unsigned>(oink::log::queuedEventCount()),
                 oink::log::writtenEventCount(),
                 oink::log::droppedEventCount(),
                 gApp.runtimeConfig.otaEnabled ? "true" : "false",
                 oink::log::sessionCsvPath(),
                 oink::log::sessionJsonlPath(),
                 oink::log::dailyCsvPath(),
                 oink::log::dailyJsonlPath());
        request->send(200, "application/json", buf);
    });
    gServer.on("/api/time", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (request->hasParam("epoch")) {
            time_t epoch = static_cast<time_t>(request->getParam("epoch")->value().toInt());
            bool ok = oink::timeutil::setFromEpoch(epoch, "browser");
            if (!ok) {
                request->send(400, "application/json", "{\"status\":\"invalid_epoch\"}");
                return;
            }
        }

        char isoBuffer[40];
        oink::timeutil::formatIso8601(isoBuffer, sizeof(isoBuffer));
        char buf[256];
        snprintf(buf,
                 sizeof(buf),
                 "{\"synced\":%s,\"time_source\":\"%s\",\"iso8601\":\"%s\",\"day_token\":\"%s\"}",
                 oink::timeutil::isSynced() ? "true" : "false",
                 oink::timeutil::timeSourceLabel(),
                 isoBuffer,
                 gApp.dayToken);
        request->send(200, "application/json", buf);
    });
    gServer.on("/api/gps", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("lat") || !request->hasParam("lon")) {
            request->send(400, "application/json", "{\"error\":\"lat,lon required\"}");
            return;
        }

        oink::scan::updateGps(request->getParam("lat")->value().toDouble(),
                              request->getParam("lon")->value().toDouble(),
                              request->hasParam("acc") ? request->getParam("acc")->value().toFloat() : 0.0f);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    gServer.on("/api/gnss", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::gnss::writeStatusJson(*resp);
        request->send(resp);
    });
    gServer.on("/api/rtc", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::rtc::writeStatusJson(*resp);
        request->send(resp);
    });
    gServer.on("/api/client-wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::uplink::writeClientWifiStatusJson(*resp);
        request->send(resp);
    });
    gServer.on("/api/client-wifi", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
               [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                   String* body = static_cast<String*>(request->_tempObject);
                   if (index == 0 || !body) {
                       body = new String();
                       body->reserve(total);
                       request->_tempObject = body;
                   }
                   for (size_t i = 0; i < len; ++i) {
                       (*body) += static_cast<char>(data[i]);
                   }
                   if (index + len != total) {
                       return;
                   }
                   String error;
                   bool ok = oink::uplink::updateClientWifiFromJson(*body, error);
                   delete body;
                   request->_tempObject = nullptr;
                   if (!ok) {
                       String payload = String("{\"status\":\"error\",\"message\":\"") + error + "\"}";
                       request->send(400, "application/json", payload);
                       return;
                   }
                   request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Client WiFi updated\"}");
               });
    gServer.on("/api/wigle", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::uplink::writeWigleStatusJson(*resp);
        request->send(resp);
    });
    gServer.on("/api/wigle", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
               [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                   String* body = static_cast<String*>(request->_tempObject);
                   if (index == 0 || !body) {
                       body = new String();
                       body->reserve(total);
                       request->_tempObject = body;
                   }
                   for (size_t i = 0; i < len; ++i) {
                       (*body) += static_cast<char>(data[i]);
                   }
                   if (index + len != total) {
                       return;
                   }
                   String error;
                   bool ok = oink::uplink::updateWigleFromJson(*body, error);
                   delete body;
                   request->_tempObject = nullptr;
                   if (!ok) {
                       String payload = String("{\"status\":\"error\",\"message\":\"") + error + "\"}";
                       request->send(400, "application/json", payload);
                       return;
                   }
                   request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"WiGLE credentials updated\"}");
               });
    gServer.on("/api/wigle/upload", HTTP_POST, [](AsyncWebServerRequest* request) {
        String error;
        if (!oink::uplink::requestWigleUpload(error)) {
            String payload = String("{\"status\":\"error\",\"message\":\"") + error + "\"}";
            request->send(400, "application/json", payload);
            return;
        }
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"WiGLE upload queued\"}");
    });
    gServer.on("/api/wardrive", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::log::writeWardriveStatusJson(*resp);
        request->send(resp);
    });
    gServer.on("/api/wardrive/debug", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::log::writeWardriveDebugJson(*resp);
        request->send(resp);
    });
    gServer.on("/api/wardrive", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
               [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                   String* body = static_cast<String*>(request->_tempObject);
                   if (index == 0 || !body) {
                       body = new String();
                       body->reserve(total);
                       request->_tempObject = body;
                   }
                   for (size_t i = 0; i < len; ++i) {
                       (*body) += static_cast<char>(data[i]);
                   }
                   if (index + len != total) {
                       return;
                   }

                   JsonDocument doc;
                   DeserializationError err = deserializeJson(doc, *body);
                   delete body;
                   request->_tempObject = nullptr;
                   if (err || !doc.is<JsonObject>() || !doc["enabled"].is<bool>()) {
                       request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"enabled boolean required\"}");
                       return;
                   }

                   String message;
                   bool ok = oink::settings::setWardriveEnabled(doc["enabled"].as<bool>(), true, message);
                   if (!ok) {
                       String payload = String("{\"status\":\"error\",\"message\":\"") + message + "\"}";
                       request->send(400, "application/json", payload);
                       return;
                   }

                   String payload = String("{\"status\":\"success\",\"message\":\"") +
                                    (message.length() ? message : "ok") + "\"}";
                   request->send(200, "application/json", payload);
               });

    gServer.on("/api/patterns", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        oink::scan::writeProfileJson(*resp);
        request->send(resp);
    });
    gServer.on("/api/patterns", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
               [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                   String* body = static_cast<String*>(request->_tempObject);
                   if (index == 0 || !body) {
                       body = new String();
                       body->reserve(total);
                       request->_tempObject = body;
                   }
                   for (size_t i = 0; i < len; ++i) {
                       (*body) += static_cast<char>(data[i]);
                   }
                   if (index + len != total) {
                       return;
                   }

                   String error;
                   bool ok = oink::scan::updateProfileFromJson(*body, error);
                   delete body;
                   request->_tempObject = nullptr;
                   if (!ok) {
                       String payload = String("{\"status\":\"error\",\"message\":\"") + error + "\"}";
                       request->send(400, "application/json", payload);
                       return;
                   }
                   request->send(200, "application/json", "{\"status\":\"success\"}");
               });
    gServer.on("/api/ota/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        writeOtaStatusJson(*resp);
        request->send(resp);
    });
    gServer.on("/api/ota",
               HTTP_POST,
               [](AsyncWebServerRequest* request) {
                   if (!gApp.runtimeConfig.otaEnabled) {
                       request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"OTA is disabled in config.\"}");
                       return;
                   }
                   if (gOtaHasError) {
                       String payload = String("{\"status\":\"error\",\"message\":\"") + gOtaLastMessage + "\"}";
                       request->send(500, "application/json", payload);
                       return;
                   }

                   setOtaMessage("Update applied. Rebooting shortly.");
                   gPendingRestartAtMs = millis() + 1500;
                   String payload = String("{\"status\":\"success\",\"message\":\"") + gOtaLastMessage + "\"}";
                   request->send(200, "application/json", payload);
               },
               [](AsyncWebServerRequest*, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
                   if (!gApp.runtimeConfig.otaEnabled) {
                       return;
                   }

                   if (index == 0) {
                       gOtaInProgress = true;
                       gOtaHasError = false;
                       gOtaBytesWritten = 0;
                       gPendingRestartAtMs = 0;
                       setOtaMessage("Starting OTA update");
                       printf("[OINK-YOU] OTA upload started: %s\n", filename.c_str());
                       if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                           char message[64];
                           snprintf(message, sizeof(message), "Update begin failed (%u)", Update.getError());
                           setOtaError(message);
                           Update.abort();
                           return;
                       }
                   }

                   if (!gOtaHasError) {
                       size_t written = Update.write(data, len);
                       if (written != len) {
                           char message[64];
                           snprintf(message, sizeof(message), "Update write failed (%u)", Update.getError());
                           setOtaError(message);
                           Update.abort();
                           return;
                       }
                       gOtaBytesWritten += written;
                   }

                   if (final) {
                       gOtaInProgress = false;
                       if (!gOtaHasError) {
                           if (Update.end(true)) {
                               setOtaMessage("Update applied. Rebooting shortly.");
                               printf("[OINK-YOU] OTA upload complete: %u bytes\n", static_cast<unsigned>(gOtaBytesWritten));
                           } else {
                               char message[64];
                               snprintf(message, sizeof(message), "Update finalize failed (%u)", Update.getError());
                               setOtaError(message);
                           }
                       } else {
                           printf("[OINK-YOU] OTA upload failed: %s\n", gOtaLastMessage);
                       }
                   }
               });

    gServer.on("/api/export/json", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Content-Disposition", "attachment; filename=\"oinkyou_detections.json\"");
        oink::log::writeDetectionsJson(resp);
        request->send(resp);
    });

    gServer.on("/api/export/csv", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("text/csv");
        resp->addHeader("Content-Disposition", "attachment; filename=\"oinkyou_detections.csv\"");
        writeCsv(resp);
        request->send(resp);
    });

    gServer.on("/api/export/kml", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/vnd.google-earth.kml+xml");
        resp->addHeader("Content-Disposition", "attachment; filename=\"oinkyou_detections.kml\"");
        oink::log::writeDetectionsKml(resp);
        request->send(resp);
    });
    gServer.on("/api/export/wigle", HTTP_GET, [](AsyncWebServerRequest* request) {
        char path[96];
        size_t fileBytes = 0;
        String error;
        if (!oink::log::prepareWigleCsv(path, sizeof(path), fileBytes, error)) {
            String payload = String("{\"status\":\"error\",\"message\":\"") + error + "\"}";
            request->send(400, "application/json", payload);
            return;
        }
        AsyncResponseStream* resp = request->beginResponseStream("text/csv");
        resp->addHeader("Content-Disposition", "attachment; filename=\"oinkyou_wigle.csv\"");
        if (!oink::log::writeSdFileToStream(path, *resp)) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to read WiGLE CSV from SD\"}");
            return;
        }
        request->send(resp);
    });

    gServer.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (oink::log::storageReady() && SPIFFS.exists(config::kPrevSessionFile)) {
            request->send(SPIFFS, config::kPrevSessionFile, "application/json");
        } else {
            request->send(200, "application/json", "[]");
        }
    });

    gServer.on("/api/history/json", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (oink::log::storageReady() && SPIFFS.exists(config::kPrevSessionFile)) {
            AsyncWebServerResponse* resp = request->beginResponse(SPIFFS, config::kPrevSessionFile, "application/json");
            resp->addHeader("Content-Disposition", "attachment; filename=\"oinkyou_prev_session.json\"");
            request->send(resp);
        } else {
            request->send(404, "application/json", "{\"error\":\"no prior session\"}");
        }
    });

    gServer.on("/api/history/kml", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!oink::log::storageReady() || !SPIFFS.exists(config::kPrevSessionFile)) {
            request->send(404, "application/json", "{\"error\":\"no prior session\"}");
            return;
        }

        File file = SPIFFS.open(config::kPrevSessionFile, "r");
        if (!file) {
            request->send(500, "text/plain", "read error");
            return;
        }

        String content = file.readString();
        file.close();
        if (content.length() == 0) {
            request->send(404, "application/json", "{\"error\":\"prior session empty\"}");
            return;
        }

        AsyncResponseStream* resp = request->beginResponseStream("application/vnd.google-earth.kml+xml");
        resp->addHeader("Content-Disposition", "attachment; filename=\"oinkyou_prev_session.kml\"");
        resp->print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n"
                    "<name>Oink-You Prior Session</name>\n"
                    "<description>Surveillance device detections from prior session</description>\n"
                    "<Style id=\"det\"><IconStyle><color>ff4489ec</color><scale>1.0</scale></IconStyle></Style>\n"
                    "<Style id=\"raven\"><IconStyle><color>ff4444ef</color><scale>1.2</scale></IconStyle></Style>\n");

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, content);
        if (!err && doc.is<JsonArray>()) {
            int placed = 0;
            for (JsonObject detection : doc.as<JsonArray>()) {
                JsonObject gps = detection["gps"];
                if (!gps || !gps["lat"].is<double>()) {
                    continue;
                }
                bool isRaven = detection["raven"] | false;
                resp->printf("<Placemark><name>%s</name>\n", detection["mac"] | "?");
                resp->printf("<styleUrl>#%s</styleUrl>\n", isRaven ? "raven" : "det");
                resp->print("<description><![CDATA[");
                if (detection["name"].is<const char*>() && strlen(detection["name"] | "") > 0) {
                    resp->printf("<b>Name:</b> %s<br/>", detection["name"] | "");
                }
                resp->printf("<b>Method:</b> %s<br/><b>RSSI:</b> %d<br/><b>Count:</b> %d",
                             detection["method"] | "?",
                             detection["rssi"] | 0,
                             detection["count"] | 1);
                if (isRaven && detection["fw"].is<const char*>()) {
                    resp->printf("<br/><b>Raven FW:</b> %s", detection["fw"] | "");
                }
                resp->print("]]></description>\n");
                resp->printf("<Point><coordinates>%.8f,%.8f,0</coordinates></Point>\n",
                             static_cast<double>(gps["lon"] | 0.0),
                             static_cast<double>(gps["lat"] | 0.0));
                resp->print("</Placemark>\n");
                ++placed;
            }
            printf("[OINK-YOU] Prior session KML: %d placemarks\n", placed);
        } else {
            printf("[OINK-YOU] Prior session KML: JSON parse failed\n");
        }

        resp->print("</Document>\n</kml>");
        request->send(resp);
    });

    gServer.on("/api/clear", HTTP_GET, [](AsyncWebServerRequest* request) {
        oink::log::saveSession();
        oink::scan::resetDetections();
        request->send(200, "application/json", "{\"status\":\"cleared\"}");
        printf("[OINK-YOU] All detections cleared (session saved)\n");
    });

    gServer.onNotFound([](AsyncWebServerRequest* request) {
        if (request->url().startsWith("/api/")) {
            request->send(404, "application/json", "{\"error\":\"not_found\"}");
            return;
        }
        sendPortalRedirect(request);
    });

    gServer.begin();
    printf("[OINK-YOU] Web server started on port 80\n");
}

} // namespace

void beginDns() {
    beginCaptivePortalDns();
}

void beginServer() {
    setupServer();
}

void poll() {
    if (gDnsServerStarted) {
        gDnsServer.processNextRequest();
    }

    if (gPendingRestartAtMs != 0 && millis() >= gPendingRestartAtMs) {
        printf("[OINK-YOU] Restarting after OTA\n");
        oink::log::endWardriveSession();
        oink::log::saveSession();
        delay(100);
        ESP.restart();
    }
}

} // namespace portal
} // namespace oink
