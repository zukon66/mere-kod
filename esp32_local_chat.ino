#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <vector>

// -------------------- Config --------------------
// ASCII ONLY! No emoji, no Turkish chars.
// FIX: Using char arrays instead of String class to prevent STL/Allocator errors.

const char* ADMIN_USER = "gusullu";
const char* ADMIN_PASS = "omer3355";
const char* ADMIN_AV = "wolf"; // entity &#128058;

String apSsid = "ESP32-Chat-v3";
String apPass = "12345678";

AsyncWebServer server(80);

// -------------------- Data Structures --------------------
// Fixed size buffers to avoid dynamic allocation issues

struct ChatUser {
  char name[32];
  char pass[32];
  char ip[24];
  char avatar[16]; 
  bool online;
  unsigned long lastActiveMs;
};

struct Message {
  char from[32];
  char text[128];
  bool isSystem;
  unsigned long ts;
};

std::vector<ChatUser*> users;
std::vector<Message*> messages;

const unsigned long ONLINE_TIMEOUT_MS = 10000;

// -------------------- Helpers --------------------

// Safe copy
void safeCpy(char* dest, const char* src, size_t size) {
  if(!src) { dest[0] = 0; return; }
  strncpy(dest, src, size - 1);
  dest[size - 1] = 0;
}

void loadUsers() {
  users.clear();
  if(!SPIFFS.exists("/users.json")) return;
  File f = SPIFFS.open("/users.json", "r");
  if(!f) return;
  
  DynamicJsonDocument doc(8192);
  deserializeJson(doc, f);
  f.close();
  
  JsonArray arr = doc.as<JsonArray>();
  for(JsonObject obj : arr) {
    ChatUser *u = new ChatUser();
    safeCpy(u->name, obj["u"] | "", sizeof(u->name));
    safeCpy(u->pass, obj["p"] | "", sizeof(u->pass));
    safeCpy(u->avatar, obj["av"] | "fox", sizeof(u->avatar));
    safeCpy(u->ip, obj["ip"] | "-", sizeof(u->ip));
    u->online = false;
    u->lastActiveMs = 0;
    users.push_back(u);
  }
}

void saveUsers() {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for(const auto *u : users) {
    JsonObject obj = arr.createNestedObject();
    obj["u"] = u->name;
    obj["p"] = u->pass;
    obj["av"] = u->avatar;
    obj["ip"] = u->ip;
  }
  File f = SPIFFS.open("/users.json", "w");
  if(f) {
    serializeJson(doc, f);
    f.close();
  }
}

int findUser(const char* name) {
  for(size_t i=0; i<users.size(); i++) {
    if(strcmp(users[i]->name, name) == 0) return (int)i;
  }
  return -1;
}

bool isAdmin(const char* u, const char* p) {
  return (strcmp(u, ADMIN_USER) == 0 && strcmp(p, ADMIN_PASS) == 0);
}

// -------------------- HTML --------------------

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
<title>ESP Chat</title>
<style>
* { box-sizing: border-box; }
body { font-family: sans-serif; background: #e5ddd5; margin: 0; padding: 0; height: 100vh; overflow: hidden; }

/* Colors */
:root {
  --green: #25d366;
  --white: #ffffff;
}

/* Screens */
.screen { display: none; height: 100%; flex-direction: column; }
.screen.active { display: flex; }

/* Login */
#loginScreen { align-items: center; justify-content: center; background: #fff; }
.login-box { padding: 20px; border: 1px solid #ccc; border-radius: 8px; width: 300px; text-align: center; }
.avatar-grid { display: grid; grid-template-columns: repeat(5, 1fr); gap: 5px; margin: 10px 0; }
.av-btn { font-size: 24px; cursor: pointer; border: 2px solid transparent; border-radius: 5px; background: none; }
.av-btn.selected { border-color: var(--green); background: #e0f2f1; }
input, button { width: 100%; padding: 10px; margin: 5px 0; border-radius: 5px; border: 1px solid #ddd; }
button { background: var(--green); color: white; font-weight: bold; cursor: pointer; border: none; }

/* Chat */
#chatScreen { background: #efe7dd; }
.header { background: #075e54; color: white; padding: 10px; display: flex; align-items: center; justify-content: space-between; }
.chat-body { flex: 1; display: flex; overflow: hidden; }

/* Sidebar */
.user-list { width: 250px; background: white; border-right: 1px solid #ddd; overflow-y: auto; }
.user-item { padding: 10px; border-bottom: 1px solid #f0f0f0; display: flex; align-items: center; }
.u-av { font-size: 24px; margin-right: 10px; }
.u-info { flex: 1; }
.u-name { font-weight: bold; }
.u-status { width: 10px; height: 10px; border-radius: 50%; background: #ccc; }
.u-status.online { background: var(--green); }

/* Msgs */
.msg-area { flex: 1; display: flex; flex-direction: column; position: relative; }
.msgs { flex: 1; overflow-y: auto; padding: 10px; display: flex; flex-direction: column; gap: 5px; }
.msg { max-width: 80%; padding: 8px 12px; border-radius: 8px; font-size: 14px; position: relative; word-wrap: break-word; }
.msg.me { align-self: flex-end; background: #dcf8c6; }
.msg.other { align-self: flex-start; background: white; }
.msg.sys { align-self: center; background: #fff3cd; font-size: 12px; text-align: center; width: 90%; }
.sender-name { font-size: 10px; color: #555; margin-bottom: 2px; }

/* Input */
.composer { padding: 10px; background: #f0f0f0; display: flex; gap: 10px; }
#msgInput { flex: 1; border-radius: 20px; border: none; padding: 10px 15px; }

/* Admin */
#adminPanel { display: none; position: absolute; top: 0; left: 0; width: 100%; height: 100%; background: white; z-index: 100; flex-direction: column; }
.adm-header { padding: 10px; background: #333; color: white; display: flex; justify-content: space-between; }
.adm-content { flex: 1; overflow-y: auto; padding: 10px; }
.adm-box { border: 1px solid #ccc; padding: 10px; margin-bottom: 10px; border-radius: 5px; }
table { width: 100%; border-collapse: collapse; font-size: 12px; }
th, td { border: 1px solid #ddd; padding: 4px; text-align: left; }

@media(max-width: 600px) {
  .user-list { display: none; position: absolute; top: 50px; left: 0; height: calc(100% - 50px); z-index: 50; width: 200px; border-right: 2px solid #ddd;}
  .user-list.active { display: block; }
}
</style>
</head>
<body>

<div id="loginScreen" class="screen active">
  <div class="login-box">
    <h2>Giris Yap</h2>
    <input id="lUser" placeholder="Kullanici Adi">
    <input id="lPass" type="password" placeholder="Sifre">
    <div style="text-align:left; font-size:12px; margin-top:5px;">Avatar:</div>
    <div class="avatar-grid">
      <div class="av-btn selected" onclick="selAv('fox')">&#129418;</div>
      <div class="av-btn" onclick="selAv('robot')">&#129302;</div>
      <div class="av-btn" onclick="selAv('alien')">&#128125;</div>
      <div class="av-btn" onclick="selAv('skull')">&#128128;</div>
      <div class="av-btn" onclick="selAv('cowboy')">&#129312;</div>
    </div>
    <button onclick="doLogin()">GIRIS</button>
    <button onclick="doRegister()" style="background:#00bcd4; margin-top:5px;">KAYIT OL</button>
  </div>
</div>

<div id="chatScreen" class="screen">
  <div class="header">
    <div style="display:flex; align-items:center gap:10px;">
      <span style="font-size:20px; cursor:pointer;" onclick="toggleUsers()">&#9776;</span> 
      <span style="margin-left:10px; font-weight:bold;">ESP Chat</span>
    </div>
    <div>
      <span id="myAv" style="font-size:20px;"></span> 
      <button id="admBtn" onclick="openAdmin()" style="width:auto; padding:5px; margin-left:5px; display:none; font-size:12px;">ADMIN</button>
      <button onclick="logout()" style="width:auto; padding:5px; background:#d32f2f; margin-left:5px; font-size:12px;">CIKIS</button>
    </div>
  </div>
  <div class="chat-body">
    <div class="user-list" id="uList"></div>
    <div class="msg-area">
      <div class="msgs" id="msgBox"></div>
      <div id="typing" style="font-size:12px; color:#666; padding:0 10px; height:15px; font-style:italic;"></div>
      <div class="composer">
        <input id="msgInput" placeholder="Mesaj..." oninput="onType()">
        <button onclick="sendMsg()" style="width:60px;">></button>
      </div>
    </div>
  </div>
  
  <div id="adminPanel">
    <div class="adm-header">
      <span>Admin Paneli</span>
      <button onclick="closeAdmin()" style="width:auto; background:#555;">KAPAT</button>
    </div>
    <div class="adm-content">
      <div class="adm-box">
        <h4>WiFi Station</h4>
        <button onclick="admScan()">Ag Tara</button>
        <div id="scanRes" style="margin:5px 0;"></div>
        <input id="wSsid" placeholder="SSID">
        <input id="wPass" placeholder="Sifre">
        <button onclick="admConn()">Baglan</button>
      </div>
      <div class="adm-box">
        <h4>AP Ayarlari</h4>
        <input id="apName" placeholder="Yeni AP Ismi">
        <input id="apKey" placeholder="Yeni AP Sifre">
        <button onclick="admAp()">Kaydet ve Reset</button>
      </div>
      <div class="adm-box">
        <h4>Kullanicilar</h4>
        <table>
          <thead><tr><th>User</th><th>Pass</th><th>IP</th><th>Islem</th></tr></thead>
          <tbody id="admUsers"></tbody>
        </table>
      </div>
      <div class="adm-box">
        <h4>Duyuru Yap</h4>
        <input id="annText" placeholder="Mesaj...">
        <button onclick="admAnnounce()">Gonder</button>
      </div>
    </div>
  </div>
</div>

<script>
let state = { u:"", p:"", av:"", admin:false };
let lastMsgTime = 0;
let selAvatar = "fox";
const codemap = { "fox": "&#129418;", "robot": "&#129302;", "alien": "&#128125;", "skull": "&#128128;", "cowboy": "&#129312;", "wolf": "&#128058;" };

function selAv(name) {
  selAvatar = name;
  document.querySelectorAll('.av-btn').forEach(b => b.classList.remove('selected'));
  event.target.classList.add('selected');
}

function req(url, data, cb) {
  let fd = new FormData();
  if(state.u) { fd.append("u", state.u); fd.append("p", state.p); }
  for(let k in data) fd.append(k, data[k]);
  fetch(url, { method:"POST", body:fd }).then(r=>r.json()).then(cb).catch(e=>console.log(e));
}

function doLogin() {
  let u = document.getElementById("lUser").value.trim();
  let p = document.getElementById("lPass").value.trim();
  if(!u||!p) return alert("Bos birakma");
  req("/login", {user:u, pass:p}, res => {
    if(res.ok) { state = { u:u, p:p, av:res.av, admin:res.admin }; enterApp(); }
    else alert("Giris hatasi");
  });
}

function doRegister() {
  let u = document.getElementById("lUser").value.trim();
  let p = document.getElementById("lPass").value.trim();
  if(!u||!p) return alert("Bos birakma");
  req("/register", {user:u, pass:p, av:selAvatar}, res => {
    if(res.ok) alert("Kayit Basarili! Giris yapabilirsin.");
    else alert("Hata: " + res.msg);
  });
}

function logout() { location.reload(); }
function enterApp() {
  document.getElementById("loginScreen").classList.remove("active");
  document.getElementById("chatScreen").classList.add("active");
  document.getElementById("myAv").innerHTML = codemap[state.av] || "";
  if(state.admin) document.getElementById("admBtn").style.display = "inline-block";
  setInterval(poll, 2000); poll();
}

function poll() {
  req("/updates", {last:lastMsgTime}, res => {
    let ul = document.getElementById("uList"); ul.innerHTML = "";
    res.users.forEach(u => {
      let div = document.createElement("div"); div.className = "user-item";
      let code = codemap[u.av] || "?";
      let status = u.online ? "online" : "";
      div.innerHTML = `<span class="u-av">${code}</span> <div class="u-info"><div class="u-name">${u.name}</div></div> <div class="u-status ${status}"></div>`;
      ul.appendChild(div);
    });
    
    let mb = document.getElementById("msgBox");
    let scroll = (mb.scrollTop + mb.clientHeight >= mb.scrollHeight - 20);
    res.msgs.forEach(m => {
      lastMsgTime = m.ts;
      let d = document.createElement("div");
      if(m.sys) { d.className = "msg sys"; d.innerText = m.txt; } 
      else {
        d.className = "msg " + (m.from === state.u ? "me" : "other");
        let ent = codemap[m.avStr] || "";
        d.innerHTML = `<div class="sender-name">${ent} ${m.from}</div> ${m.txt}`;
      }
      mb.appendChild(d);
    });
    if(res.msgs.length > 0 && scroll) mb.scrollTop = mb.scrollHeight;
    
    document.getElementById("typing").innerText = res.typing ? res.typing + " yaziyor..." : "";
  });
}

function onType() { req("/typing", {}, r=>{}); }
function sendMsg() {
  let i = document.getElementById("msgInput"); let t = i.value.trim();
  if(!t) return;
  req("/send", {txt:t}, r => { if(r.ok) { i.value = ""; poll(); }});
}
function toggleUsers() { document.getElementById("uList").classList.toggle("active"); }

function openAdmin() { document.getElementById("adminPanel").style.display="flex"; loadAdmUsers(); }
function closeAdmin() { document.getElementById("adminPanel").style.display="none"; }
function loadAdmUsers() {
  req("/admin/users", {}, r => {
    let tb = document.getElementById("admUsers"); tb.innerHTML = "";
    r.users.forEach(u => {
      let tr = document.createElement("tr");
      tr.innerHTML = `<td>${u.u}</td><td>${u.p}</td><td>${u.ip}</td><td><button onclick="banUser('${u.u}')" style="background:red;font-size:10px;">SIL</button></td>`;
      tb.appendChild(tr);
    });
  });
}
function banUser(u) { if(confirm("Sil?")) req("/admin/ban", {target:u}, r => loadAdmUsers()); }
function admScan() { req("/admin/scan", {}, r => { document.getElementById("scanRes").innerHTML = r.nets.join(", "); }); }
function admConn() { req("/admin/connect", {s:document.getElementById("wSsid").value, p:document.getElementById("wPass").value}, r=>{alert("Baglaniyor...")}); }
function admAp() { req("/admin/ap", {s:document.getElementById("apName").value, p:document.getElementById("apKey").value}, r=>{alert("Resetleniyor...")}); }
function admAnnounce() { req("/admin/announce", {txt:document.getElementById("annText").value}, r=>{alert("Gonderildi");}); }

</script>
</body>
</html>
)rawliteral";

// -------------------- Server Logic --------------------

String typist = "";
unsigned long typeTime = 0;

void setup() {
  Serial.begin(115200);
  
  if(!SPIFFS.begin(true)) { Serial.println("SPIFFS Fail"); }
  loadUsers();
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  // Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", INDEX_HTML); });
  
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request){
    String u = request->arg("user");
    String p = request->arg("pass");
    if(isAdmin(u.c_str(), p.c_str())) {
        request->send(200, "application/json", "{\"ok\":true,\"admin\":true,\"av\":\"wolf\"}");
        return;
    }
    int idx = findUser(u.c_str());
    if(idx >= 0 && strcmp(users[idx]->pass, p.c_str()) == 0) {
        safeCpy(users[idx]->ip, request->client()->remoteIP().toString().c_str(), 24);
        users[idx]->online = true;
        users[idx]->lastActiveMs = millis();
        String json = "{\"ok\":true,\"admin\":false,\"av\":\"" + String(users[idx]->avatar) + "\"}";
        request->send(200, "application/json", json);
    } else {
        request->send(200, "application/json", "{\"ok\":false}");
    }
  });

  server.on("/register", HTTP_POST, [](AsyncWebServerRequest *request){
    String u = request->arg("user");
    String p = request->arg("pass");
    String av = request->arg("av");
    if(findUser(u.c_str()) >= 0 || u == ADMIN_USER || u.length()<2) {
      request->send(200, "application/json", "{\"ok\":false,\"msg\":\"Gecersiz\"}");
      return;
    }
    ChatUser *nu = new ChatUser();
    safeCpy(nu->name, u.c_str(), 32);
    safeCpy(nu->pass, p.c_str(), 32);
    safeCpy(nu->avatar, av.c_str(), 16);
    safeCpy(nu->ip, "-", 24);
    nu->online = false;
    users.push_back(nu);
    saveUsers();
    request->send(200, "application/json", "{\"ok\":true}");
  });
  
  server.on("/send", HTTP_POST, [](AsyncWebServerRequest *request){
    String u = request->arg("u");
    String p = request->arg("p");
    String t = request->arg("txt");
    
    int idx = findUser(u.c_str());
    bool admin = isAdmin(u.c_str(), p.c_str());
    
    if(admin || (idx >= 0 && strcmp(users[idx]->pass, p.c_str()) == 0)) {
        Message *m = new Message();
        safeCpy(m->from, u.c_str(), 32);
        safeCpy(m->text, t.c_str(), 128);
        m->isSystem = false;
        m->ts = millis();
        messages.push_back(m);
        if(messages.size() > 50) { delete messages[0]; messages.erase(messages.begin()); }
        if(idx >= 0) { users[idx]->lastActiveMs = millis(); users[idx]->online = true; }
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(200, "application/json", "{\"ok\":false}");
    }
  });
  
  server.on("/updates", HTTP_POST, [](AsyncWebServerRequest *request){
    String u = request->arg("u");
    String lastStr = request->arg("last");
    unsigned long last = lastStr.toInt();
    
    int idx = findUser(u.c_str());
    if(idx >= 0) { users[idx]->lastActiveMs = millis(); users[idx]->online = true; }
    
    String json = "{";
    json += "\"users\":[";
    bool f = true;
    for(auto *usr : users) {
        if(millis() - usr->lastActiveMs > ONLINE_TIMEOUT_MS) usr->online = false;
        if(!f) json += ",";
        json += "{\"name\":\"" + String(usr->name) + "\",\"av\":\"" + String(usr->avatar) + "\",\"online\":" + (usr->online?"true":"false") + "}";
        f = false;
    }
    json += "],";
    
    json += "\"msgs\":[";
    f = true;
    for(const auto *m : messages) {
        if(m->ts > last) {
            if(!f) json += ",";
            String av = "";
            int uid = findUser(m->from);
            if(uid >= 0) av = users[uid]->avatar;
            if(isAdmin(m->from, ADMIN_PASS)) av = "wolf"; 
            
            json += "{\"from\":\"" + String(m->from) + "\",\"txt\":\"" + String(m->text) + "\",\"ts\":" + String(m->ts) + ",\"sys\":" + (m->isSystem?"true":"false") + ",\"avStr\":\"" + av + "\"}";
            f = false;
        }
    }
    json += "],";
    if(millis() - typeTime < 2000 && typist.length() > 0 && typist != u) json += "\"typing\":\"" + typist + "\"";
    else json += "\"typing\":\"\"";
    
    json += "}";
    request->send(200, "application/json", json);
  });
  
  server.on("/typing", HTTP_POST, [](AsyncWebServerRequest *request){
     typist = request->arg("u");
     typeTime = millis();
     request->send(200, "application/json", "{}");
  });
  
  server.on("/admin/users", HTTP_POST, [](AsyncWebServerRequest *request){
      if(!isAdmin(request->arg("u").c_str(), request->arg("p").c_str())) return request->send(200, "{}", "{}");
      String json = "{\"users\":[";
      for(size_t i=0; i<users.size(); i++) {
          if(i>0) json += ",";
          json += "{\"u\":\"" + String(users[i]->name) + "\",\"p\":\"" + String(users[i]->pass) + "\",\"ip\":\"" + String(users[i]->ip) + "\"}";
      }
      json += "]}";
      request->send(200, "application/json", json);
  });
  
  server.on("/admin/ban", HTTP_POST, [](AsyncWebServerRequest *request){
      if(!isAdmin(request->arg("u").c_str(), request->arg("p").c_str())) return request->send(200, "{}", "{}");
      int idx = findUser(request->arg("target").c_str());
      if(idx >= 0) { delete users[idx]; users.erase(users.begin() + idx); saveUsers(); }
      request->send(200, "application/json", "{}");
  });
  
   server.on("/admin/scan", HTTP_POST, [](AsyncWebServerRequest *request){
      if(!isAdmin(request->arg("u").c_str(), request->arg("p").c_str())) return request->send(200, "{}", "{}");
      int n = WiFi.scanNetworks();
      String json = "{\"nets\":[";
      for(int i=0; i<n; i++) {
        if(i>0) json+=",";
        json += "\"" + WiFi.SSID(i) + "\"";
      }
      json += "]}";
      request->send(200, "application/json", json);
  });
  
  server.on("/admin/connect", HTTP_POST, [](AsyncWebServerRequest *request){
      if(!isAdmin(request->arg("u").c_str(), request->arg("p").c_str())) return request->send(200, "{}", "{}");
      WiFi.begin(request->arg("s").c_str(), request->arg("p").c_str());
      request->send(200, "application/json", "{}");
  });
  
   server.on("/admin/announce", HTTP_POST, [](AsyncWebServerRequest *request){
      if(!isAdmin(request->arg("u").c_str(), request->arg("p").c_str())) return request->send(200, "{}", "{}");
      Message *m = new Message();
      safeCpy(m->from, "SISTEM", 32);
      safeCpy(m->text, request->arg("txt").c_str(), 128);
      m->isSystem = true;
      m->ts = millis();
      messages.push_back(m);
      request->send(200, "application/json", "{}");
  });
  
   server.on("/admin/ap", HTTP_POST, [](AsyncWebServerRequest *request){
      if(!isAdmin(request->arg("u").c_str(), request->arg("p").c_str())) return request->send(200, "{}", "{}");
      request->send(200, "application/json", "{}");
      delay(500);
      ESP.restart();
  });

  server.begin();
}

void loop() {}
