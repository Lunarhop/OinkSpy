#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <cstring>

#include "oink_board.h"
#include "oink_config.h"
#include "oink_log.h"
#include "oink_scan.h"
#include "oink_settings.h"
#include "oink_state.h"
#include "oink_time.h"

using namespace oink;

namespace {

AsyncWebServer gServer(80);

static const char FY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>OinkSpy</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
html,body{height:100%;overflow:hidden}
body{font-family:'Courier New',monospace;background:#1f0f15;color:#fff1f5;display:flex;flex-direction:column}
.hd{background:#34131e;padding:10px 14px;border-bottom:2px solid #fb7185;flex-shrink:0}
.hd h1{font-size:22px;color:#fda4af;letter-spacing:2px}
.hd .sub{font-size:11px;color:#f9a8d4;margin-top:2px}
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
.pig{float:right;font-size:16px;color:#fbcfe8}
.ev{background:rgba(72,28,43,.4);border:1px solid rgba(251,113,133,.18);border-radius:7px;padding:9px;margin-bottom:8px}
.ev .hdx{display:flex;justify-content:space-between;gap:8px;font-size:11px;color:#fbcfe8}
.ev .tp{font-weight:bold;color:#fda4af;letter-spacing:1px}
.ev .lb{display:inline-block;margin-top:6px;padding:3px 6px;border-radius:999px;background:rgba(251,113,133,.15);font-size:11px}
.ev .meta{display:flex;flex-wrap:wrap;gap:5px;margin-top:6px;font-size:11px}
.ev .meta span{background:rgba(251,113,133,.1);padding:3px 6px;border-radius:4px}
</style></head><body>
<div class="hd"><h1>OINKSPY <span class="pig">(^oo^)</span></h1><div class="sub">Pig-themed surveillance detector &bull; wardriving + GPS</div></div>
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
<div id="dL"><div class="empty">Snout up, scanning for surveillance gear...<br>BLE active on all channels</div></div>
</div>
<div class="pn" id="p1"><div id="hL"><div class="empty">Loading prior session...</div></div></div>
<div class="pn" id="p2"><div id="pC">Loading patterns...</div></div>
<div class="pn" id="p3">
<h4>EXPORT DETECTIONS</h4>
<p style="font-size:10px;color:#f9a8d4;margin-bottom:8px">Download current session to import into the companion dashboard</p>
<button class="btn" onclick="location.href='/api/export/json'">DOWNLOAD JSON</button>
<button class="btn" onclick="location.href='/api/export/csv'">DOWNLOAD CSV</button>
<button class="btn" onclick="location.href='/api/export/kml'" style="background:#22c55e">DOWNLOAD KML (GPS MAP)</button>
<hr class="sep">
<h4>PRIOR SESSION</h4>
<button class="btn" onclick="location.href='/api/history/json'" style="background:#6366f1">DOWNLOAD PREV JSON</button>
<button class="btn" onclick="location.href='/api/history/kml'" style="background:#22c55e">DOWNLOAD PREV KML</button>
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
function tab(i,el){document.querySelectorAll('.tb button').forEach(b=>b.classList.remove('a'));document.querySelectorAll('.pn').forEach(p=>p.classList.remove('a'));el.classList.add('a');document.getElementById('p'+i).classList.add('a');if(i===1&&!window._hL)loadHistory();if(i===2&&!window._pL)loadPat();if(i===3)loadEvents();}
function refresh(){fetch('/api/detections').then(r=>r.json()).then(d=>{D=d;render();stats();}).catch(()=>{});}
function render(){const el=document.getElementById('dL');if(!D.length){el.innerHTML='<div class="empty">Snout up, scanning for surveillance gear...<br>BLE active on all channels</div>';return;} D.sort((a,b)=>b.last-a.last);el.innerHTML=D.map(card).join('');}
function stats(){document.getElementById('sT').textContent=D.length;document.getElementById('sR').textContent=D.filter(d=>d.raven).length; fetch('/api/stats').then(r=>r.json()).then(s=>{let g=document.getElementById('sG');if(s.gps_valid){g.textContent=s.gps_tagged+'/'+s.total;g.style.color='#22c55e';}else{g.textContent='OFF';g.style.color='#ef4444';}}).catch(()=>{});}
function card(d){return '<div class="det"><div class="mac">'+d.mac+(d.name?'<span class="nm">'+d.name+'</span>':'')+'</div><div class="inf"><span>RSSI: '+d.rssi+'</span><span>'+d.method+'</span><span style="color:#fda4af;font-weight:bold">&times;'+d.count+'</span>'+(d.raven?'<span class="rv">RAVEN '+d.fw+'</span>':'')+(d.gps?'<span style="color:#22c55e">&#9673; '+d.gps.lat.toFixed(5)+','+d.gps.lon.toFixed(5)+'</span>':'<span style="color:#666">no gps</span>')+'</div></div>';}
function eventCard(e){let title=e.record_type==='bookmark'?'BOOKMARK':'DETECTION';let label=e.label?'<div class="lb">'+e.label+'</div>':'';let gps=e.gps?'<span>GPS '+e.gps.lat.toFixed(5)+','+e.gps.lon.toFixed(5)+'</span>':'';let who=e.mac?'<span>'+e.mac+(e.name?' '+e.name:'')+'</span>':'';let sig=e.record_type==='detection'?'<span>RSSI '+e.rssi+'</span><span>'+e.method+'</span><span>&times;'+e.count+'</span>':'';let rv=e.is_raven?'<span>RAVEN '+e.raven_fw+'</span>':'';return '<div class="ev"><div class="hdx"><span class="tp">'+title+'</span><span>'+(e.iso8601||('ms '+e.millis))+'</span></div>'+label+'<div class="meta"><span>boot '+e.boot_count+'</span><span>'+e.time_source+'</span>'+who+sig+rv+gps+'</div></div>';}
function loadHistory(){fetch('/api/history').then(r=>r.json()).then(d=>{H=d;let el=document.getElementById('hL');if(!H.length){el.innerHTML='<div class="empty">No prior session data</div>';return;} H.sort((a,b)=>b.last-a.last);el.innerHTML='<div style="font-size:11px;color:#f9a8d4;margin-bottom:8px">'+H.length+' detections from prior session</div>'+H.map(card).join('');window._hL=1;}).catch(()=>{document.getElementById('hL').innerHTML='<div class="empty">No prior session data</div>';});}
function loadPat(){fetch('/api/patterns').then(r=>r.json()).then(p=>{let h=''; h+='<div class="pg"><h3>Oink MAC Prefixes ('+p.macs.length+')</h3><div class="it">'+p.macs.map(m=>'<span>'+m+'</span>').join('')+'</div></div>'; h+='<div class="pg"><h3>Contract Mfr MACs ('+p.macs_mfr.length+')</h3><div class="it">'+p.macs_mfr.map(m=>'<span>'+m+'</span>').join('')+'</div></div>'; h+='<div class="pg"><h3>SoundThinking MACs ('+p.macs_soundthinking.length+')</h3><div class="it">'+p.macs_soundthinking.map(m=>'<span>'+m+'</span>').join('')+'</div></div>'; h+='<div class="pg"><h3>BLE Device Names ('+p.names.length+')</h3><div class="it">'+p.names.map(n=>'<span>'+n+'</span>').join('')+'</div></div>'; h+='<div class="pg"><h3>BLE Manufacturer IDs ('+p.mfr.length+')</h3><div class="it">'+p.mfr.map(m=>'<span>0x'+m.toString(16).toUpperCase().padStart(4,'0')+'</span>').join('')+'</div></div>'; h+='<div class="pg"><h3>Raven UUIDs ('+p.raven.length+')</h3><div class="it">'+p.raven.map(u=>'<span style="font-size:8px">'+u+'</span>').join('')+'</div></div>'; document.getElementById('pC').innerHTML=h;window._pL=1;}).catch(()=>{});}
function loadEvents(){fetch('/api/events').then(r=>r.json()).then(d=>{E=d;let el=document.getElementById('eL');if(!E.length){el.innerHTML='<div class="empty">No recent events yet</div>';return;} el.innerHTML=E.map(eventCard).join('');}).catch(()=>{document.getElementById('eL').innerHTML='<div class="empty">Recent events unavailable</div>';});}
let _gW=null,_gOk=false,_gTried=false;
function sendGPS(p){_gOk=true;let g=document.getElementById('sG');g.textContent='OK';g.style.color='#22c55e'; fetch('/api/gps?lat='+p.coords.latitude+'&lon='+p.coords.longitude+'&acc='+(p.coords.accuracy||0)).catch(()=>{});}
function gpsErr(e){_gOk=false;let g=document.getElementById('sG'); var msg='ERR';if(e.code===1){msg='DENIED';g.style.color='#ef4444';alert('GPS permission denied. On iPhone, GPS requires HTTPS which this device cannot provide. On Android Chrome, tap the lock/info icon in the address bar and allow Location.');} else if(e.code===2){msg='N/A';g.style.color='#ef4444';} else if(e.code===3){msg='WAIT';g.style.color='#facc15';} g.textContent=msg;}
function startGPS(){if(!navigator.geolocation){return false;} if(_gW!==null){navigator.geolocation.clearWatch(_gW);_gW=null;} let g=document.getElementById('sG');g.textContent='...';g.style.color='#facc15'; _gW=navigator.geolocation.watchPosition(sendGPS,gpsErr,{enableHighAccuracy:true,maximumAge:5000,timeout:15000});return true;}
function reqGPS(){if(!navigator.geolocation){alert('GPS not available in this browser.');return;} if(_gOk){return;} if(!window.isSecureContext){alert('GPS requires a secure context (HTTPS). This HTTP page may not get GPS permission.\n\nAndroid Chrome: try chrome://flags and enable "Insecure origins treated as secure", add http://192.168.4.1\n\niPhone: GPS will not work over HTTP.');} startGPS();_gTried=true;}
function syncClock(){fetch('/api/time?epoch='+Math.floor(Date.now()/1000)).catch(()=>{});}
refresh();loadEvents();syncClock();setInterval(refresh,2500);setInterval(loadEvents,3000);setInterval(syncClock,60000);
</script></body></html>
)rawliteral";

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
        request->send(200, "text/html", FY_HTML);
    });

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
                 "{\"total\":%d,\"raven\":%d,\"ble\":\"active\",\"gps_valid\":%s,\"gps_age\":%lu,\"gps_tagged\":%d,\"sd_ready\":%s,\"sd_logging_healthy\":%s,\"time_synced\":%s,\"time_source\":\"%s\",\"day_token\":\"%s\"}",
                 gApp.detectionCount,
                 oink::scan::countRavenDetections(),
                 oink::scan::gpsIsFresh() ? "true" : "false",
                 gApp.gpsValid ? (millis() - gApp.gpsLastUpdate) : 0UL,
                 oink::scan::countGpsTaggedDetections(),
                 gApp.sdReady ? "true" : "false",
                 gApp.sdLoggingHealthy ? "true" : "false",
                 oink::timeutil::isSynced() ? "true" : "false",
                 oink::timeutil::timeSourceLabel(),
                 gApp.dayToken);
        request->send(200, "application/json", buf);
    });

    gServer.on("/api/storage", HTTP_GET, [](AsyncWebServerRequest* request) {
        char buf[640];
        snprintf(buf,
                 sizeof(buf),
                 "{\"spiffs_ready\":%s,\"sd_ready\":%s,\"sd_logging_enabled\":%s,\"log_worker_ready\":%s,\"log_queue_depth\":%u,\"log_events_written\":%lu,\"log_events_dropped\":%lu,\"session_csv\":\"%s\",\"session_jsonl\":\"%s\",\"daily_csv\":\"%s\",\"daily_jsonl\":\"%s\"}",
                 gApp.spiffsReady ? "true" : "false",
                 gApp.sdReady ? "true" : "false",
                 gApp.runtimeConfig.sdLoggingEnabled ? "true" : "false",
                 gApp.logWorkerReady ? "true" : "false",
                 static_cast<unsigned>(oink::log::queuedEventCount()),
                 oink::log::writtenEventCount(),
                 oink::log::droppedEventCount(),
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

    gServer.on("/api/patterns", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        size_t count = 0;
        const char* const* strings = nullptr;
        const uint16_t* ids = nullptr;

        resp->print("{\"macs\":[");
        strings = oink::scan::flockMacPrefixes(count);
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) resp->print(",");
            resp->printf("\"%s\"", strings[i]);
        }

        resp->print("],\"macs_mfr\":[");
        strings = oink::scan::flockManufacturerPrefixes(count);
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) resp->print(",");
            resp->printf("\"%s\"", strings[i]);
        }

        resp->print("],\"macs_soundthinking\":[");
        strings = oink::scan::soundThinkingPrefixes(count);
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) resp->print(",");
            resp->printf("\"%s\"", strings[i]);
        }

        resp->print("],\"names\":[");
        strings = oink::scan::deviceNamePatterns(count);
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) resp->print(",");
            resp->printf("\"%s\"", strings[i]);
        }

        resp->print("],\"mfr\":[");
        ids = oink::scan::bleManufacturerIds(count);
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) resp->print(",");
            resp->printf("%u", ids[i]);
        }

        resp->print("],\"raven\":[");
        strings = oink::scan::ravenServiceUuids(count);
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) resp->print(",");
            resp->printf("\"%s\"", strings[i]);
        }
        resp->print("]}");
        request->send(resp);
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
                if (!gps || !gps.containsKey("lat")) {
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

    gServer.begin();
    printf("[OINK-YOU] Web server started on port 80\n");
}

void processControlEvents() {
    oink::board::ControlEvent event = oink::board::ControlEvent::None;
    while (oink::board::nextControlEvent(event)) {
        switch (event) {
            case oink::board::ControlEvent::ShortPress:
                oink::scan::setScanningEnabled(!oink::scan::isScanningEnabled());
                oink::board::addNotification(oink::scan::isScanningEnabled() ? "SCAN RESUMED" : "SCAN PAUSED");
                oink::board::confirmBeep();
                printf("[OINK-YOU] Button short press: scanning %s\n", oink::scan::isScanningEnabled() ? "ENABLED" : "PAUSED");
                break;
            case oink::board::ControlEvent::LongPress:
                oink::board::toggleAudioMode();
                printf("[OINK-YOU] Button long press: audio %s\n", gApp.buzzerOn ? "ON" : "SILENT");
                break;
            case oink::board::ControlEvent::DoublePress:
                oink::log::appendBookmarkEvent("manual");
                oink::board::addNotification("BOOKMARK SAVED");
                oink::board::bookmarkBeep();
                printf("[OINK-YOU] Button double press: bookmark saved\n");
                break;
            case oink::board::ControlEvent::None:
            default:
                break;
        }
    }
}
void pollSerialHost() {
    if (Serial.available()) {
        while (Serial.available()) {
            Serial.read();
        }
        gApp.lastSerialHeartbeat = millis();
        if (!gApp.serialHostConnected) {
            gApp.serialHostConnected = true;
            gApp.companionChangePending = true;
        }
        return;
    }

    if (gApp.serialHostConnected && millis() - gApp.lastSerialHeartbeat >= gApp.runtimeConfig.serialTimeoutMs) {
        gApp.serialHostConnected = false;
        gApp.companionChangePending = true;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    gApp.negotiatedMtu = 23;
    gApp.mutex = xSemaphoreCreateMutex();

    oink::board::initializePins();
    oink::log::beginStorage();
    oink::settings::load();
    oink::timeutil::begin();
    oink::log::prepareSdLogs();

    printf("\n========================================\n");
    printf("  OINK-YOU Surveillance Detector + OLED\n");
    printf("  Buzzer: %s\n", gApp.buzzerOn ? "ON" : "OFF");
    printf("========================================\n");

    oink::scan::setupBle();
    oink::board::bootBeep();
    oink::board::initializeDisplay();

    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAP(gApp.runtimeConfig.apSsid, gApp.runtimeConfig.apPassword);
    printf("[OINK-YOU] AP: %s / %s\n", gApp.runtimeConfig.apSsid, gApp.runtimeConfig.apPassword);
    printf("[OINK-YOU] IP: %s\n", WiFi.softAPIP().toString().c_str());

    setupServer();

    printf("[OINK-YOU] Board map: button D1, SD CS D2, buzzer D3, OLED SDA/SCL D4/D5, GPS UART D6/D7, SPI D8-D10\n");
    printf("[OINK-YOU] Device ID: %s, boot #%lu\n", oink::settings::deviceId(), oink::settings::bootCount());
    printf("[OINK-YOU] SD logging: %s\n", gApp.sdReady && gApp.runtimeConfig.sdLoggingEnabled ? "ENABLED" : "DISABLED");
    printf("[OINK-YOU] Log worker: %s\n", gApp.logWorkerReady ? "READY" : "DISABLED");
    printf("[OINK-YOU] Time source: %s (%s)\n", oink::timeutil::timeSourceLabel(), gApp.dayToken);
    if (gApp.sdReady && gApp.runtimeConfig.sdLoggingEnabled) {
        printf("[OINK-YOU] SD session CSV: %s\n", oink::log::sessionCsvPath());
        printf("[OINK-YOU] SD session JSONL: %s\n", oink::log::sessionJsonlPath());
    }
    printf("[OINK-YOU] Controls: short=scan toggle, long=audio mode, double=bookmark\n");
    printf("[OINK-YOU] Detection methods: MAC prefix, device name, manufacturer ID, Raven UUID\n");
    printf("[OINK-YOU] Dashboard: http://192.168.4.1\n");
    printf("[OINK-YOU] Ready - BLE GATT + AP mode + OLED\n\n");
}

void loop() {
    oink::timeutil::poll();
    oink::board::pollControls();
    processControlEvents();
    oink::board::serviceUi();
    pollSerialHost();

    if (gApp.companionChangePending) {
        gApp.companionChangePending = false;
        oink::scan::onCompanionChange();
    }

    oink::scan::pollScan();
    oink::log::pollAutoSave();
    delay(20);
}






















