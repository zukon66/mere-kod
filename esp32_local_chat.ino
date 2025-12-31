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
<title>ESP32 Local Chat</title>
<style>
:root{
  --bg:#121212;
  --panel:#1f1f1f;
  --panel2:#2c2c2c;
  --accent:#b0e0e6;
  --text:#eaeaea;
  --muted:#b0b0b0;
  --green:#00c853;
  --gray:#666;
}
*{box-sizing:border-box}
body{
  margin:0;
  font-family: "Trebuchet MS", Arial, sans-serif;
  background:var(--bg);
  color:var(--text);
}
header{
  padding:14px 16px;
  background:linear-gradient(135deg, #1b1b1b, #111);
  border-bottom:1px solid #222;
  display:flex;
  align-items:center;
  justify-content:space-between;
}
header h1{font-size:18px;margin:0;color:var(--accent)}
#main{
  display:flex;
  flex-direction:row;
  gap:12px;
  padding:12px;
}
.panel{
  background:var(--panel);
  border:1px solid #222;
  border-radius:10px;
  padding:10px;
}
#left{
  width:260px;
  min-width:220px;
}
#chat{
  flex:1;
  display:flex;
  flex-direction:column;
  gap:8px;
}
#users .user{
  padding:6px 8px;
  border-bottom:1px solid #222;
  display:flex;
  align-items:center;
  gap:8px;
  cursor:pointer;
}
.dot{
  width:8px;height:8px;border-radius:50%;
  background:var(--gray);
}
.dot.online{background:var(--green)}
#messages{
  height:360px;
  overflow:auto;
  background:var(--panel2);
  border-radius:8px;
  padding:8px;
  border:1px solid #222;
}
.msg{
  margin:6px 0;
  padding:8px 10px;
  border-radius:10px;
  max-width:80%;
  font-size:14px;
}
.msg.me{background:var(--accent); color:#101010; margin-left:auto}
.msg.other{background:#1b1b1b; color:var(--text)}
#composer{
  display:flex;
  gap:6px;
}
input, select, button{
  background:#1b1b1b;
  border:1px solid #333;
  color:var(--text);
  padding:8px;
  border-radius:8px;
}
button{background:var(--accent); color:#101010; cursor:pointer}
#login, #register{
  display:flex;
  gap:6px;
  flex-wrap:wrap;
}
#adminPanel{
  margin-top:10px;
  display:none;
}
.section-title{
  font-size:14px;
  color:var(--accent);
  margin:8px 0 6px 0;
}
table{
  width:100%;
  border-collapse:collapse;
  font-size:12px;
}
th, td{
  border-bottom:1px solid #222;
  padding:6px;
  text-align:left;
}
@media (max-width:900px){
  #main{flex-direction:column}
  #left{width:100%}
  #messages{height:260px}
}
</style>
</head>
<body>
<header>
  <h1>ESP32 Local Chat</h1>
  <div id="status">Disconnected</div>
</header>

<div id="main">
  <div id="left" class="panel">
    <div class="section-title">Giris / Kayit</div>
    <div id="login">
      <input id="loginUser" placeholder="Rumuz">
      <input id="loginPass" type="password" placeholder="Sifre">
      <button onclick="login()">Giris</button>
    </div>
    <div id="register" style="margin-top:6px;">
      <input id="regUser" placeholder="Rumuz">
      <input id="regPass" type="password" placeholder="Sifre">
      <button onclick="registerUser()">Kayit</button>
    </div>

    <div class="section-title" style="margin-top:12px;">Kullanicilar</div>
    <div id="users"></div>

    <div class="section-title">Gruplar</div>
    <div id="groups"></div>
    <div id="groupCreate" style="margin-top:6px;">
      <input id="newGroupName" placeholder="Yeni grup adi">
      <input id="newGroupMembers" placeholder="Uyeler (virgul)">
      <button onclick="createGroup()">Olustur</button>
    </div>

    <div id="adminPanel" class="panel" style="margin-top:10px;">
      <div class="section-title">Admin Paneli</div>
      <div>
        <div class="section-title">AP Ayarlari</div>
        <input id="apSsid" placeholder="AP SSID">
        <input id="apPass" placeholder="AP Sifre">
        <button onclick="saveAp()">Kaydet ve Yeniden Baslat</button>
      </div>
      <div style="margin-top:8px;">
        <div class="section-title">Wi-Fi Baglanti</div>
        <button onclick="scanWifi()">Aglari Tara</button>
        <select id="wifiList"></select>
        <input id="wifiPass" placeholder="Secilen ag sifresi">
        <button onclick="connectWifi()">Baglan</button>
      </div>
      <div style="margin-top:8px;">
        <div class="section-title">Kullanicilar</div>
        <table>
          <thead><tr><th>Rumuz</th><th>Sifre</th><th>IP</th></tr></thead>
          <tbody id="adminUsers"></tbody>
        </table>
      </div>
    </div>
  </div>

  <div id="chat" class="panel">
    <div class="section-title">Sohbet</div>
    <div id="targetInfo">Hedef: -</div>
    <div id="messages"></div>
    <div id="composer">
      <input id="msgText" style="flex:1;" placeholder="Mesaj yaz...">
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
      const dot=document.createElement("div");
      dot.className="dot "+(u.online?"online":"");
      const span=document.createElement("span");
      span.innerText=u.name;
      d.appendChild(dot); d.appendChild(span);
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
      const dot=document.createElement("div");
      dot.className="dot online";
      const span=document.createElement("span");
      span.innerText=g.name;
      d.appendChild(dot); d.appendChild(span);
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
