#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <vector>

WebServer server(80);
Preferences prefs;

// -------------------- Config --------------------
const char* ADMIN_USER = "gusullu";
const char* ADMIN_PASS = "omer3355";

String apSsid = "ESP32-CHAT";
String apPass = "12345678";

struct User {
  String name;
  String pass;
  String ip;
  bool online;
};

struct Message {
  String from;
  String to;
  String text;
  bool isGroup;
  unsigned long ts;
};

struct Group {
  String name;
  std::vector<String> members;
};

std::vector<User> users;
std::vector<Message> messages;
std::vector<Group> groups;

// -------------------- Helpers --------------------
String htmlEscape(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

int findUser(const String &name) {
  for (size_t i = 0; i < users.size(); i++) {
    if (users[i].name == name) return (int)i;
  }
  return -1;
}

bool isAdmin(const String &u, const String &p) {
  return (u == ADMIN_USER && p == ADMIN_PASS);
}

void saveWifiPrefs(const String &ssid, const String &pass) {
  prefs.begin("wifi", false);
  prefs.putString("ap_ssid", ssid);
  prefs.putString("ap_pass", pass);
  prefs.end();
}

void loadWifiPrefs() {
  prefs.begin("wifi", true);
  String s = prefs.getString("ap_ssid", "");
  String p = prefs.getString("ap_pass", "");
  prefs.end();
  if (s.length() > 0) apSsid = s;
  if (p.length() > 0) apPass = p;
}

// -------------------- Web UI --------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, shrink-to-fit=no">
<title>ESP32 Chat</title>
<style>
:root{
  --bg:#111b21;
  --panel:#202c33;
  --chat-bg:#0b141a;
  --in-msg:#202c33;
  --out-msg:#005c4b;
  --accent:#00a884;
  --text:#e9edef;
  --muted:#8696a0;
  --divider:#2a3942;
}
*{box-sizing:border-box}
body{
  margin:0;
  font-family: "Segoe UI", "Helvetica Neue", Helvetica, Arial, sans-serif;
  background:var(--bg);
  color:var(--text);
}
#main{
  display:flex;
  height:100vh;
  width:100vw;
}
.sidebar{
  width:320px;
  min-width:260px;
  background:var(--panel);
  border-right:1px solid var(--divider);
  display:flex;
  flex-direction:column;
}
.sidebar-header{
  padding:14px 16px;
  display:flex;
  align-items:center;
  justify-content:space-between;
  border-bottom:1px solid var(--divider);
}
.sidebar-title{
  font-size:16px;
  font-weight:600;
}
#status{
  font-size:12px;
  color:#0b141a;
  background:var(--accent);
  padding:4px 8px;
  border-radius:999px;
}
.sidebar-body{
  padding:10px;
  display:flex;
  flex-direction:column;
  gap:10px;
  overflow:auto;
}
.section-title{
  font-size:11px;
  color:var(--muted);
  text-transform:uppercase;
  letter-spacing:0.6px;
  margin:2px 0 6px 0;
}
.card{
  background:#111b21;
  border:1px solid var(--divider);
  border-radius:10px;
  padding:8px;
}
input, select, button{
  width:100%;
  background:#111b21;
  border:1px solid var(--divider);
  color:var(--text);
  padding:10px;
  border-radius:8px;
  outline:none;
}
button{
  background:var(--accent);
  color:#0b141a;
  border:0;
  cursor:pointer;
  font-weight:600;
}
#login, #register{
  display:flex;
  flex-direction:column;
  gap:6px;
}
.search{
  background:#111b21;
  border:1px solid var(--divider);
  color:var(--text);
  padding:8px 10px;
  border-radius:8px;
}
.list{
  border:1px solid var(--divider);
  border-radius:10px;
  overflow:auto;
  max-height:200px;
}
.user{
  display:flex;
  align-items:center;
  gap:10px;
  padding:10px;
  border-bottom:1px solid var(--divider);
  cursor:pointer;
}
.user:last-child{border-bottom:none}
.user:hover{background:#1a2328}
.avatar{
  width:36px;
  height:36px;
  border-radius:50%;
  background:#2a3942;
  display:flex;
  align-items:center;
  justify-content:center;
  font-weight:600;
}
.user-meta{flex:1; min-width:0}
.user-name{font-size:14px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis}
.user-sub{font-size:12px; color:var(--muted)}
.dot{
  width:8px;height:8px;border-radius:50%;
  background:#55626b;
}
.dot.online{background:#25d366}
.chat{
  flex:1;
  display:flex;
  flex-direction:column;
  background:var(--chat-bg);
}
.chat-header{
  background:var(--panel);
  padding:12px 16px;
  border-bottom:1px solid var(--divider);
  display:flex;
  align-items:center;
  justify-content:space-between;
}
.chat-title{
  font-size:14px;
  font-weight:600;
}
.chat-sub{
  font-size:12px;
  color:var(--muted);
}
#messages{
  flex:1;
  padding:16px;
  overflow:auto;
  background:
    radial-gradient(circle at 20% 20%, rgba(255,255,255,0.03) 0, rgba(255,255,255,0) 35%),
    linear-gradient(180deg, #0b141a 0%, #0b141a 100%);
}
.msg{
  position:relative;
  margin:6px 0;
  padding:8px 12px;
  border-radius:8px;
  max-width:78%;
  font-size:14px;
  line-height:1.35;
}
.msg.other{
  background:var(--in-msg);
  color:var(--text);
  border-top-left-radius:2px;
}
.msg.me{
  background:var(--out-msg);
  color:var(--text);
  margin-left:auto;
  border-top-right-radius:2px;
}
.msg.other:before{
  content:"";
  position:absolute;
  left:-6px;
  top:6px;
  width:0;height:0;
  border-top:6px solid transparent;
  border-bottom:6px solid transparent;
  border-right:6px solid var(--in-msg);
}
.msg.me:after{
  content:"";
  position:absolute;
  right:-6px;
  top:6px;
  width:0;height:0;
  border-top:6px solid transparent;
  border-bottom:6px solid transparent;
  border-left:6px solid var(--out-msg);
}
#composer{
  padding:12px 16px;
  background:var(--panel);
  border-top:1px solid var(--divider);
  display:flex;
  gap:10px;
}
#msgText{flex:1}
@media (max-width:900px){
  #main{flex-direction:column}
  .sidebar{width:100%;min-width:0;height:55vh}
  .chat{height:45vh}
}
</style>
</head>
<body>
<div id="main">
  <div class="sidebar">
    <div class="sidebar-header">
      <div class="sidebar-title">ESP32 Chat</div>
      <div id="status">Disconnected</div>
    </div>
    <div class="sidebar-body">
      <div class="card">
        <div class="section-title">Giris</div>
        <div id="login">
          <input id="loginUser" placeholder="Rumuz">
          <input id="loginPass" type="password" placeholder="Sifre">
          <button onclick="login()">Giris</button>
        </div>
        <div class="section-title" style="margin-top:10px;">Kayit</div>
        <div id="register">
          <input id="regUser" placeholder="Rumuz">
          <input id="regPass" type="password" placeholder="Sifre">
          <button onclick="registerUser()">Kayit</button>
        </div>
      </div>

      <input class="search" placeholder="Ara...">

      <div>
        <div class="section-title">Kullanicilar</div>
        <div id="users" class="list"></div>
      </div>

      <div>
        <div class="section-title">Gruplar</div>
        <div id="groups" class="list"></div>
      </div>

      <div id="groupCreate" class="card">
        <div class="section-title">Yeni Grup</div>
        <input id="newGroupName" placeholder="Grup adi">
        <input id="newGroupMembers" placeholder="Uyeler (virgul)">
        <button onclick="createGroup()">Olustur</button>
      </div>

      <div id="adminPanel" class="card">
        <div class="section-title">Admin Paneli</div>
        <div class="card" style="margin-bottom:8px;">
          <div class="section-title">AP Ayarlari</div>
          <input id="apSsid" placeholder="AP SSID">
          <input id="apPass" placeholder="AP Sifre">
          <button onclick="saveAp()">Kaydet ve Yeniden Baslat</button>
        </div>
        <div class="card" style="margin-bottom:8px;">
          <div class="section-title">Wi-Fi Baglanti</div>
          <button onclick="scanWifi()">Aglari Tara</button>
          <select id="wifiList"></select>
          <input id="wifiPass" placeholder="Secilen ag sifresi">
          <button onclick="connectWifi()">Baglan</button>
        </div>
        <div class="card">
          <div class="section-title">Kullanicilar</div>
          <table>
            <thead><tr><th>Rumuz</th><th>Sifre</th><th>IP</th></tr></thead>
            <tbody id="adminUsers"></tbody>
          </table>
        </div>
      </div>
    </div>
  </div>

  <div class="chat">
    <div class="chat-header">
      <div>
        <div class="chat-title">Sohbet</div>
        <div id="targetInfo" class="chat-sub">Hedef: -</div>
      </div>
      <div class="chat-sub">Yerel</div>
    </div>
    <div id="messages"></div>
    <div id="composer">
      <input id="msgText" placeholder="Mesaj yaz...">
      <button onclick="sendMsg()">Gonder</button>
    </div>
  </div>
</div>

<script>
let currentUser = "";
let currentPass = "";
let currentTarget = "";
let currentTargetIsGroup = false;

function setStatus(t){document.getElementById("status").innerText=t;}

function login(){
  const u=document.getElementById("loginUser").value.trim();
  const p=document.getElementById("loginPass").value.trim();
  if(!u||!p)return;
  fetch(`/login?u=${encodeURIComponent(u)}&p=${encodeURIComponent(p)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok){
      currentUser=u; currentPass=p;
      setStatus("Online: "+u);
      document.getElementById("adminPanel").style.display = j.admin ? "block" : "none";
      refreshAll();
    }else alert("Giris basarisiz");
  });
}

function registerUser(){
  const u=document.getElementById("regUser").value.trim();
  const p=document.getElementById("regPass").value.trim();
  if(!u||!p)return;
  fetch(`/register?u=${encodeURIComponent(u)}&p=${encodeURIComponent(p)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok) alert("Kayit basarili");
    else alert("Kayit basarisiz: "+j.msg);
  });
}

function refreshAll(){
  fetch("/users").then(r=>r.json()).then(j=>{
    const cont=document.getElementById("users");
    cont.innerHTML="";
    j.users.forEach(u=>{
      const d=document.createElement("div");
      d.className="user";
      const avatar=document.createElement("div");
      avatar.className="avatar";
      avatar.innerText=(u.name && u.name.length>0)?u.name[0].toUpperCase():"?";
      const meta=document.createElement("div");
      meta.className="user-meta";
      const name=document.createElement("div");
      name.className="user-name";
      name.innerText=u.name;
      const sub=document.createElement("div");
      sub.className="user-sub";
      sub.innerText=u.online?"Cevrimici":"Cevrimdisi";
      const dot=document.createElement("div");
      dot.className="dot "+(u.online?"online":"");
      meta.appendChild(name);
      meta.appendChild(sub);
      d.appendChild(avatar);
      d.appendChild(meta);
      d.appendChild(dot);
      d.onclick=()=>{selectTarget(u.name,false);}
      cont.appendChild(d);
    });
  });

  fetch("/groups").then(r=>r.json()).then(j=>{
    const cont=document.getElementById("groups");
    cont.innerHTML="";
    j.groups.forEach(g=>{
      const d=document.createElement("div");
      d.className="user";
      const avatar=document.createElement("div");
      avatar.className="avatar";
      avatar.innerText="#";
      const meta=document.createElement("div");
      meta.className="user-meta";
      const name=document.createElement("div");
      name.className="user-name";
      name.innerText=g.name;
      const sub=document.createElement("div");
      sub.className="user-sub";
      sub.innerText="Grup";
      const dot=document.createElement("div");
      dot.className="dot online";
      meta.appendChild(name);
      meta.appendChild(sub);
      d.appendChild(avatar);
      d.appendChild(meta);
      d.appendChild(dot);
      d.onclick=()=>{selectTarget(g.name,true);}
      cont.appendChild(d);
    });
  });

  if(currentUser){
    fetch(`/adminUsers?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`)
    .then(r=>r.json()).then(j=>{
      if(!j.ok)return;
      const tbody=document.getElementById("adminUsers");
      tbody.innerHTML="";
      j.users.forEach(u=>{
        const tr=document.createElement("tr");
        tr.innerHTML=`<td>${u.name}</td><td>${u.pass}</td><td>${u.ip}</td>`;
        tbody.appendChild(tr);
      });
    });
  }
}

function selectTarget(name,isGroup){
  currentTarget=name;
  currentTargetIsGroup=isGroup;
  document.getElementById("targetInfo").innerText="Hedef: "+name+(isGroup?" (Grup)":"");
  loadMessages();
}

function loadMessages(){
  if(!currentUser || !currentTarget) return;
  fetch(`/messages?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&t=${encodeURIComponent(currentTarget)}&g=${currentTargetIsGroup?1:0}`)
  .then(r=>r.json()).then(j=>{
    const cont=document.getElementById("messages");
    cont.innerHTML="";
    j.messages.forEach(m=>{
      const div=document.createElement("div");
      div.className="msg "+(m.from===currentUser?"me":"other");
      div.innerText = (m.from===currentUser ? "Ben: " : (m.from+": "))+m.text;
      cont.appendChild(div);
    });
    cont.scrollTop=cont.scrollHeight;
  });
}

function sendMsg(){
  const t=document.getElementById("msgText").value.trim();
  if(!t||!currentTarget||!currentUser)return;
  fetch(`/send?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&t=${encodeURIComponent(currentTarget)}&g=${currentTargetIsGroup?1:0}&m=${encodeURIComponent(t)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok){
      document.getElementById("msgText").value="";
      loadMessages();
    }
  });
}

function createGroup(){
  const name=document.getElementById("newGroupName").value.trim();
  const members=document.getElementById("newGroupMembers").value.trim();
  if(!name||!members||!currentUser)return;
  fetch(`/groupCreate?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&name=${encodeURIComponent(name)}&members=${encodeURIComponent(members)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok){alert("Grup olustu"); refreshAll();}
    else alert("Grup olusmadi: "+j.msg);
  });
}

function saveAp(){
  const s=document.getElementById("apSsid").value.trim();
  const p=document.getElementById("apPass").value.trim();
  fetch(`/adminAp?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`)
  .then(r=>r.json()).then(j=>{ if(j.ok) alert("Kaydedildi, resetleniyor"); else alert("Yetki yok");});
}

function scanWifi(){
  fetch(`/adminScan?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`)
  .then(r=>r.json()).then(j=>{
    const sel=document.getElementById("wifiList");
    sel.innerHTML="";
    if(!j.ok)return;
    j.ssids.forEach(s=>{
      const opt=document.createElement("option");
      opt.value=s; opt.innerText=s;
      sel.appendChild(opt);
    });
  });
}

function connectWifi(){
  const s=document.getElementById("wifiList").value;
  const p=document.getElementById("wifiPass").value.trim();
  fetch(`/adminConnect?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok) alert("Baglaniyor...");
    else alert("Yetki yok");
  });
}

setInterval(()=>{ if(currentUser){ refreshAll(); loadMessages(); } }, 3000);
</script>
</body>
</html>
)rawliteral";

// -------------------- Routes --------------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleRegister() {
  String u = server.arg("u");
  String p = server.arg("p");
  if (u.length() < 2 || p.length() < 2) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"kisa\"}");
    return;
  }
  if (u == ADMIN_USER) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"kullanilamaz\"}");
    return;
  }
  if (findUser(u) >= 0) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"var\"}");
    return;
  }
  User nu;
  nu.name = u;
  nu.pass = p;
  nu.ip = server.client().remoteIP().toString();
  nu.online = false;
  users.push_back(nu);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleLogin() {
  String u = server.arg("u");
  String p = server.arg("p");
  if (isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":true,\"admin\":true}");
    return;
  }
  int idx = findUser(u);
  if (idx >= 0 && users[idx].pass == p) {
    users[idx].online = true;
    users[idx].ip = server.client().remoteIP().toString();
    server.send(200, "application/json", "{\"ok\":true,\"admin\":false}");
  } else {
    server.send(200, "application/json", "{\"ok\":false}");
  }
}

void handleUsers() {
  String out = "{\"users\":[";
  for (size_t i = 0; i < users.size(); i++) {
    out += "{\"name\":\"" + htmlEscape(users[i].name) + "\",\"online\":" + String(users[i].online ? "true" : "false") + "}";
    if (i + 1 < users.size()) out += ",";
  }
  out += "]}";
  server.send(200, "application/json", out);
}

void handleGroups() {
  String out = "{\"groups\":[";
  for (size_t i = 0; i < groups.size(); i++) {
    out += "{\"name\":\"" + htmlEscape(groups[i].name) + "\"}";
    if (i + 1 < groups.size()) out += ",";
  }
  out += "]}";
  server.send(200, "application/json", out);
}

void handleSend() {
  String u = server.arg("u");
  String p = server.arg("p");
  String t = server.arg("t");
  String m = server.arg("m");
  bool isG = server.arg("g") == "1";

  bool ok = false;
  if (isAdmin(u, p)) ok = true;
  else {
    int idx = findUser(u);
    if (idx >= 0 && users[idx].pass == p) ok = true;
  }
  if (!ok) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }

  Message msg;
  msg.from = u;
  msg.to = t;
  msg.text = m;
  msg.isGroup = isG;
  msg.ts = millis();
  messages.push_back(msg);

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleMessages() {
  String u = server.arg("u");
  String p = server.arg("p");
  String t = server.arg("t");
  bool isG = server.arg("g") == "1";

  bool ok = false;
  if (isAdmin(u, p)) ok = true;
  else {
    int idx = findUser(u);
    if (idx >= 0 && users[idx].pass == p) ok = true;
  }
  if (!ok) {
    server.send(200, "application/json", "{\"messages\":[]}");
    return;
  }

  String out = "{\"messages\":[";
  bool first = true;
  for (size_t i = 0; i < messages.size(); i++) {
    Message &msg = messages[i];
    bool match = false;
    if (isG) {
      match = msg.isGroup && msg.to == t;
    } else {
      match = !msg.isGroup && ((msg.from == u && msg.to == t) || (msg.from == t && msg.to == u));
    }
    if (match) {
      if (!first) out += ",";
      first = false;
      out += "{\"from\":\"" + htmlEscape(msg.from) + "\",\"text\":\"" + htmlEscape(msg.text) + "\"}";
    }
  }
  out += "]}";
  server.send(200, "application/json", out);
}

void handleGroupCreate() {
  String u = server.arg("u");
  String p = server.arg("p");
  String name = server.arg("name");
  String members = server.arg("members");

  bool ok = false;
  if (isAdmin(u, p)) ok = true;
  else {
    int idx = findUser(u);
    if (idx >= 0 && users[idx].pass == p) ok = true;
  }
  if (!ok) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"yetki\"}");
    return;
  }

  for (size_t i = 0; i < groups.size(); i++) {
    if (groups[i].name == name) {
      server.send(200, "application/json", "{\"ok\":false,\"msg\":\"var\"}");
      return;
    }
  }

  Group g;
  g.name = name;
  g.members.clear();

  int start = 0;
  while (true) {
    int comma = members.indexOf(',', start);
    String part = (comma == -1) ? members.substring(start) : members.substring(start, comma);
    part.trim();
    if (part.length() > 0) g.members.push_back(part);
    if (comma == -1) break;
    start = comma + 1;
  }
  g.members.push_back(u);

  groups.push_back(g);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleAdminUsers() {
  String u = server.arg("u");
  String p = server.arg("p");
  if (!isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  String out = "{\"ok\":true,\"users\":[";
  for (size_t i = 0; i < users.size(); i++) {
    out += "{\"name\":\"" + htmlEscape(users[i].name) + "\",\"pass\":\"" + htmlEscape(users[i].pass) + "\",\"ip\":\"" + users[i].ip + "\"}";
    if (i + 1 < users.size()) out += ",";
  }
  out += "]}";
  server.send(200, "application/json", out);
}

void handleAdminAp() {
  String u = server.arg("u");
  String p = server.arg("p");
  if (!isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() < 2 || pass.length() < 8) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  saveWifiPrefs(ssid, pass);
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void handleAdminScan() {
  String u = server.arg("u");
  String p = server.arg("p");
  if (!isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  int n = WiFi.scanNetworks();
  String out = "{\"ok\":true,\"ssids\":[";
  for (int i = 0; i < n; i++) {
    out += "\"" + htmlEscape(WiFi.SSID(i)) + "\"";
    if (i + 1 < n) out += ",";
  }
  out += "]}";
  server.send(200, "application/json", out);
}

void handleAdminConnect() {
  String u = server.arg("u");
  String p = server.arg("p");
  if (!isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  server.send(200, "application/json", "{\"ok\":true}");
}

// -------------------- Setup/Loop --------------------
void setup() {
  Serial.begin(115200);
  loadWifiPrefs();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  server.on("/", handleRoot);
  server.on("/register", handleRegister);
  server.on("/login", handleLogin);
  server.on("/users", handleUsers);
  server.on("/groups", handleGroups);
  server.on("/send", handleSend);
  server.on("/messages", handleMessages);
  server.on("/groupCreate", handleGroupCreate);
  server.on("/adminUsers", handleAdminUsers);
  server.on("/adminAp", handleAdminAp);
  server.on("/adminScan", handleAdminScan);
  server.on("/adminConnect", handleAdminConnect);

  server.begin();
}

void loop() {
  server.handleClient();
}
