struct User;

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
  String device;
  bool online;
  unsigned long lastActiveMs;
  unsigned long lastLoginMs;
  std::vector<String> blocked;
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
  String owner;
  std::vector<String> members;
};

std::vector<User> users;
std::vector<Message> messages;
std::vector<Group> groups;
std::vector<String> bannedIps;

const unsigned long ONLINE_TIMEOUT_MS = 15000;

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

bool ipBanned(const String &ip) {
  for (size_t i = 0; i < bannedIps.size(); i++) {
    if (bannedIps[i] == ip) return true;
  }
  return false;
}

void updateOnline(User &u) {
  unsigned long now = millis();
  if (u.online && (now - u.lastActiveMs) > ONLINE_TIMEOUT_MS) {
    u.online = false;
  }
}

bool userBlocks(const User &u, const String &other) {
  for (size_t i = 0; i < u.blocked.size(); i++) {
    if (u.blocked[i] == other) return true;
  }
  return false;
}

bool isBlockedPair(const String &a, const String &b) {
  int ia = findUser(a);
  int ib = findUser(b);
  if (ia < 0 || ib < 0) return false;
  if (userBlocks(users[ia], b)) return true;
  if (userBlocks(users[ib], a)) return true;
  return false;
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
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
<title>ESP Chat</title>
<style>
*{box-sizing:border-box}
body{
  font-family:sans-serif;
  background-color:#d1d7db;
  margin:0;
  height:100vh;
  overflow:hidden;
}
.screen{display:none;height:100vh;width:100vw}
.screen.active{display:flex}
#screen-login{flex-direction:column;background:#d1d7db}
#screen-app{flex-direction:column}

.login-header{
  background:#008069;
  color:#ffffff;
  padding:15px 16px;
  display:flex;
  align-items:center;
  justify-content:space-between;
}
.login-title{font-size:18px;font-weight:bold}
.status-pill{font-size:12px;font-weight:bold}
.login-wrap{flex:1;display:flex;align-items:flex-start;justify-content:center;padding:16px}
.login-box{
  width:95%;
  max-width:520px;
  background:#ffffff;
  border:1px solid #eee;
  border-radius:10px;
  padding:16px;
}

#main{height:100%;width:100vw;display:flex}
.sidebar{
  width:320px;
  min-width:260px;
  background:#ffffff;
  display:flex;
  flex-direction:column;
  border-right:1px solid #e6e6e6;
}
.sidebar-header{
  background:#008069;
  color:#ffffff;
  padding:14px 16px;
  display:flex;
  align-items:center;
  justify-content:space-between;
}
.sidebar-title{font-size:18px;font-weight:bold}
#status{font-size:12px;font-weight:bold}
.sidebar-body{
  flex:1;
  overflow:auto;
  background:#ffffff;
  padding:10px;
  display:flex;
  flex-direction:column;
  gap:10px;
}
.section-title{font-size:12px;color:#666;font-weight:bold;margin:6px 0 4px 0}
.card{background:#ffffff;border:1px solid #eee;border-radius:6px;padding:10px}
#login, #register{display:flex;flex-direction:column;gap:6px}
input, select, button{
  width:100%;
  padding:10px;
  border:1px solid #ddd;
  border-radius:20px;
  outline:none;
}
button{background:#008069;color:#ffffff;border:none;font-weight:bold;cursor:pointer}
.search{background:#ffffff;border:1px solid #ddd;border-radius:20px;padding:8px 12px}
.list{background:#ffffff;border:1px solid #eee;border-radius:6px;overflow:auto;max-height:200px}
.user{display:flex;align-items:center;gap:10px;padding:12px;border-bottom:1px solid #eee;cursor:pointer}
.user:last-child{border-bottom:none}
.avatar{width:40px;height:40px;background:#ddd;border-radius:50%;display:flex;align-items:center;justify-content:center;color:#ffffff;font-weight:bold}
.user-meta{flex:1;min-width:0}
.user-name{font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.user-sub{font-size:12px;color:#666}
.dot{width:8px;height:8px;border-radius:50%;background:#bbb}
.dot.online{background:#00a884}

.chat{flex:1;display:flex;flex-direction:column;background:#e5ddd5}
.chat-header{background:#008069;color:#ffffff;padding:14px 16px;display:flex;align-items:center;justify-content:space-between}
.chat-title{font-size:16px;font-weight:bold}
.chat-sub{font-size:12px}
#messages{flex:1;overflow:auto;padding:10px;display:flex;flex-direction:column}
.msg{max-width:75%;padding:8px;margin:3px 0;border-radius:7px;font-size:14px;word-wrap:break-word}
.msg.other{background:#ffffff;align-self:flex-start}
.msg.me{background:#dcf8c6;align-self:flex-end}
#composer{background:#f0f0f0;padding:8px;display:flex;gap:8px}
#msgText{flex:1;border-radius:20px;border:none}
#composer button{width:40px;height:40px;border-radius:50%;padding:0}

#adminPanel{display:none}

.fab{
  position:fixed;
  bottom:18px;
  width:52px;
  height:52px;
  border-radius:50%;
  border:none;
  background:#008069;
  color:#ffffff;
  font-size:20px;
  display:flex;
  align-items:center;
  justify-content:center;
  cursor:pointer;
  box-shadow:0 4px 10px rgba(0,0,0,0.2);
}
#groupFab{right:18px;display:none}
#adminFab{left:18px;display:none}

.modal{
  display:none;
  position:fixed;
  top:0;left:0;width:100%;height:100%;
  background:rgba(0,0,0,0.4);
  align-items:center;
  justify-content:center;
  z-index:50;
}
.modal .modal-content{
  width:90%;
  max-width:520px;
  background:#ffffff;
  border-radius:8px;
  padding:16px;
  border:1px solid #eee;
}
.modal .modal-actions{display:flex;gap:8px;margin-top:10px}
.modal .modal-actions button{border-radius:8px}

.logout-btn{background:#d32f2f;color:#ffffff}
.gear-btn{background:#ffffff;color:#008069;border:1px solid #ffffff;border-radius:14px;padding:6px 10px}

@media (max-width:900px){
  #main{flex-direction:column}
  .sidebar{width:100%;min-width:0;height:55vh}
  .chat{height:45vh}
}
</style>
</head>
<body>
<div id="screen-login" class="screen active">
  <div class="login-header">
    <div class="login-title">ESP Chat</div>
    <div id="statusLogin" class="status-pill">Disconnected</div>
  </div>
  <div class="login-wrap">
    <div class="login-box">
      <div class="section-title">Giris</div>
      <div id="login">
        <input id="loginUser" placeholder="Rumuz">
        <input id="loginPass" type="password" placeholder="Sifre">
        <button onclick="login()">GIRIS YAP</button>
      </div>
      <div class="section-title" style="margin-top:10px;">Kayit</div>
      <div id="register">
        <input id="regUser" placeholder="Rumuz">
        <input id="regPass" type="password" placeholder="Sifre">
        <button onclick="registerUser()">KAYIT OL</button>
      </div>
    </div>
  </div>
</div>

<div id="screen-app" class="screen">
  <div id="main">
    <div class="sidebar">
      <div class="sidebar-header">
        <div class="sidebar-title">ESP Chat</div>
        <div style="display:flex;gap:8px;align-items:center;">
          <button class="gear-btn" onclick="openSettings()">Ayarlar</button>
          <div id="status">Disconnected</div>
        </div>
      </div>
      <div class="sidebar-body">
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
          <input id="newGroupName" placeholder="Grup Ismi">
          <input id="newGroupMembers" placeholder="Uyeler (virgul)">
          <button onclick="createGroup()">OLUSTUR</button>
        </div>

        <div id="adminPanel" class="card">
          <div class="section-title">Admin Paneli</div>
          <div class="card" style="margin-bottom:8px;">
            <div class="section-title">ESP Durum</div>
            <div id="espStatus" style="font-size:12px;color:#333;">-</div>
            <div id="espClients" style="font-size:12px;color:#333;">-</div>
          </div>
          <div class="card" style="margin-bottom:8px;">
            <div class="section-title">AP Ayarlari</div>
            <input id="apSsid" placeholder="AP SSID">
            <input id="apPass" placeholder="AP Sifre">
            <button onclick="saveAp()">Kaydet ve Yeniden Baslat</button>
            <button onclick="adminReboot()" style="margin-top:6px;">ESP Yeniden Baslat</button>
          </div>
          <div class="card" style="margin-bottom:8px;">
            <div class="section-title">Wi-Fi Baglanti</div>
            <button onclick="scanWifi()">Ag Tara & Baglan</button>
            <select id="wifiList"></select>
            <input id="wifiPass" placeholder="Secilen ag sifresi">
            <button onclick="connectWifi()">Baglan</button>
          </div>
          <div class="card">
            <div class="section-title">Kullanicilar</div>
            <table>
              <thead><tr><th>Rumuz</th><th>Sifre</th><th>IP</th><th>Durum</th><th>Aktif</th><th>Islem</th></tr></thead>
              <tbody id="adminUsers"></tbody>
            </table>
          </div>
        </div>
      </div>
    </div>

    <div class="chat">
      <div class="chat-header">
        <div class="chat-title">Sohbet</div>
        <div id="targetInfo" class="chat-sub">Hedef: -</div>
      </div>
      <div id="messages"></div>
      <div id="composer">
        <input id="msgText" placeholder="Mesaj...">
        <button onclick="sendMsg()">></button>
      </div>
    </div>
  </div>
</div>

<button id="groupFab" class="fab" onclick="openGroupModal()">+</button>
<button id="adminFab" class="fab" onclick="toggleAdmin()">ADM</button>

<div id="settingsModal" class="modal">
  <div class="modal-content">
    <div class="section-title">Ayarlar</div>
    <input id="setName" placeholder="Yeni rumuz">
    <button onclick="changeName()">Isim Degistir</button>
    <input id="setPass" type="password" placeholder="Yeni sifre" style="margin-top:8px;">
    <button onclick="changePass()">Sifre Degistir</button>
    <div class="section-title" style="margin-top:10px;">Kullanici Engelle</div>
    <select id="blockUserSelect"></select>
    <button onclick="blockUser()">Engelle</button>
    <div class="modal-actions">
      <button class="logout-btn" onclick="logout()">CIKIS</button>
      <button onclick="closeSettings()">Kapat</button>
    </div>
  </div>
</div>

<div id="groupModal" class="modal">
  <div class="modal-content">
    <div class="section-title">Yeni Grup</div>
    <input id="groupName" placeholder="Grup Ismi">
    <div id="groupUserList" style="max-height:160px;overflow:auto;border:1px solid #eee;border-radius:6px;padding:6px;"></div>
    <div class="modal-actions">
      <button onclick="submitGroupModal()">OLUSTUR</button>
      <button onclick="closeGroupModal()">IPTAL</button>
    </div>
  </div>
</div>

<script>
let currentUser = "";
let currentPass = "";
let currentTarget = "";
let currentTargetIsGroup = false;
let isAdminUser = false;

function setStatus(t){
  const s=document.getElementById("status");
  if(s) s.innerText=t;
  const sl=document.getElementById("statusLogin");
  if(sl) sl.innerText=t;
}
function showScreen(id){
  document.getElementById("screen-login").classList.remove("active");
  document.getElementById("screen-app").classList.remove("active");
  document.getElementById(id).classList.add("active");
  const gf=document.getElementById("groupFab");
  if (gf) gf.style.display = (id === "screen-app") ? "flex" : "none";
}
function logout(){
  const u=currentUser;
  const p=currentPass;
  currentUser="";
  currentPass="";
  currentTarget="";
  currentTargetIsGroup=false;
  isAdminUser=false;
  setStatus("Disconnected");
  document.getElementById("adminPanel").style.display="none";
  document.getElementById("adminFab").style.display="none";
  showScreen("screen-login");
  fetch(`/logout?u=${encodeURIComponent(u)}&p=${encodeURIComponent(p)}`);
}

function login(){
  const u=document.getElementById("loginUser").value.trim();
  const p=document.getElementById("loginPass").value.trim();
  if(!u||!p)return;
  fetch(`/login?u=${encodeURIComponent(u)}&p=${encodeURIComponent(p)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok){
      currentUser=u; currentPass=p; isAdminUser=j.admin;
      setStatus("Online: "+u);
      document.getElementById("adminPanel").style.display = j.admin ? "block" : "none";
      document.getElementById("adminFab").style.display = j.admin ? "flex" : "none";
      refreshAll();
      showScreen("screen-app");
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

function openSettings(){
  document.getElementById("settingsModal").style.display="flex";
}
function closeSettings(){
  document.getElementById("settingsModal").style.display="none";
}
function changeName(){
  const n=document.getElementById("setName").value.trim();
  if(!n) return;
  fetch(`/user/updateName?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&n=${encodeURIComponent(n)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok){
      currentUser=n;
      setStatus("Online: "+n);
      alert("Guncellendi");
      refreshAll();
    } else alert("Isim degistirilemedi: "+j.msg);
  });
}
function changePass(){
  const n=document.getElementById("setPass").value.trim();
  if(!n) return;
  fetch(`/user/updatePass?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&n=${encodeURIComponent(n)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok){ currentPass=n; alert("Guncellendi"); }
    else alert("Sifre degistirilemedi");
  });
}
function blockUser(){
  const b=document.getElementById("blockUserSelect").value;
  if(!b) return;
  fetch(`/user/block?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&b=${encodeURIComponent(b)}`)
  .then(r=>r.json()).then(j=>{
    if(j.ok){ alert("Engellendi"); refreshAll(); }
    else alert("Engelleme basarisiz");
  });
}

function toggleAdmin(){
  const p=document.getElementById("adminPanel");
  if(!isAdminUser) return;
  p.style.display = (p.style.display === "none" || p.style.display === "") ? "block" : "none";
}

function openGroupModal(){
  document.getElementById("groupModal").style.display="flex";
  fetch(`/users?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`)
  .then(r=>r.json()).then(j=>{
    const cont=document.getElementById("groupUserList");
    cont.innerHTML="";
    j.users.forEach(u=>{
      if(u.name === currentUser) return;
      const row=document.createElement("div");
      row.innerHTML = `<label><input type="checkbox" value="${u.name}"> ${u.name}</label>`;
      cont.appendChild(row);
    });
  });
}
function closeGroupModal(){
  document.getElementById("groupModal").style.display="none";
}
function submitGroupModal(){
  const name=document.getElementById("groupName").value.trim();
  if(!name) return;
  const checks=document.querySelectorAll('#groupUserList input[type="checkbox"]:checked');
  let members="";
  checks.forEach(c=>{ members += c.value + ","; });
  document.getElementById("newGroupName").value=name;
  document.getElementById("newGroupMembers").value=members;
  closeGroupModal();
  createGroup();
}

function refreshAll(){
  fetch(`/users?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`)
  .then(r=>r.json()).then(j=>{
    const cont=document.getElementById("users");
    cont.innerHTML="";
    const sel=document.getElementById("blockUserSelect");
    sel.innerHTML="";
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
      sub.innerText=(u.online?"Cevrimici":"Cevrimdisi")+" | "+u.ip+" | "+u.last+"s";
      const dot=document.createElement("div");
      dot.className="dot "+(u.online?"online":"");
      meta.appendChild(name);
      meta.appendChild(sub);
      d.appendChild(avatar);
      d.appendChild(meta);
      d.appendChild(dot);
      d.onclick=()=>{selectTarget(u.name,false);}
      cont.appendChild(d);
      if(u.name !== currentUser){
        const opt=document.createElement("option");
        opt.value=u.name; opt.innerText=u.name;
        sel.appendChild(opt);
      }
    });
  });

  fetch(`/groups?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`)
  .then(r=>r.json()).then(j=>{
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

  if(currentUser && isAdminUser){
    fetch(`/adminUsers?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`)
    .then(r=>r.json()).then(j=>{
      if(!j.ok)return;
      const tbody=document.getElementById("adminUsers");
      tbody.innerHTML="";
      j.users.forEach(u=>{
        const tr=document.createElement("tr");
        const status=u.online?"on":"off";
        tr.innerHTML=`<td>${u.name}</td><td>${u.pass}</td><td>${u.ip}</td><td>${status}</td><td>${u.last}s</td><td><button onclick="adminKick('${u.name}')">At</button><button onclick="adminDelete('${u.name}')">Sil</button></td>`;
        tbody.appendChild(tr);
      });
    });

    fetch(`/admin/status?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`)
    .then(r=>r.json()).then(j=>{
      if(!j.ok)return;
      document.getElementById("espStatus").innerText = "IP: "+j.ip+" | SSID: "+j.ssid+" | RSSI: "+j.rssi;
      document.getElementById("espClients").innerText = "AP Istasyon: "+j.sta;
    });
  }
}

function adminKick(name){
  fetch(`/admin/kick?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&t=${encodeURIComponent(name)}`)
  .then(r=>r.json()).then(j=>{ if(j.ok) refreshAll(); });
}
function adminDelete(name){
  fetch(`/admin/delete?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}&t=${encodeURIComponent(name)}`)
  .then(r=>r.json()).then(j=>{ if(j.ok) refreshAll(); });
}
function adminReboot(){
  fetch(`/admin/reboot?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`)
  .then(r=>r.json()).then(j=>{ if(j.ok) alert("Yeniden baslatiliyor"); });
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
  if(!name||!currentUser)return;
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

function ping(){
  if(!currentUser) return;
  fetch(`/ping?u=${encodeURIComponent(currentUser)}&p=${encodeURIComponent(currentPass)}`);
}

setInterval(()=>{ if(currentUser){ refreshAll(); loadMessages(); } }, 3000);
setInterval(()=>{ ping(); }, 4000);
showScreen("screen-login");
</script>
</body>
</html>
)rawliteral";

// -------------------- Routes --------------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handlePing() {
  String u = server.arg("u");
  String p = server.arg("p");
  int idx = findUser(u);
  if (idx >= 0 && users[idx].pass == p) {
    users[idx].lastActiveMs = millis();
    users[idx].online = true;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleLogout() {
  String u = server.arg("u");
  String p = server.arg("p");
  int idx = findUser(u);
  if (idx >= 0 && users[idx].pass == p) {
    users[idx].online = false;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleUserUpdateName() {
  String u = server.arg("u");
  String p = server.arg("p");
  String n = server.arg("n");
  int idx = findUser(u);
  if (idx < 0 || users[idx].pass != p) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  if (n.length() < 2 || n == ADMIN_USER || findUser(n) >= 0) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"invalid\"}");
    return;
  }
  users[idx].name = n;
  for (size_t i = 0; i < groups.size(); i++) {
    if (groups[i].owner == u) groups[i].owner = n;
    for (size_t m = 0; m < groups[i].members.size(); m++) {
      if (groups[i].members[m] == u) groups[i].members[m] = n;
    }
  }
  for (size_t i = 0; i < messages.size(); i++) {
    if (messages[i].from == u) messages[i].from = n;
    if (messages[i].to == u) messages[i].to = n;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleUserUpdatePass() {
  String u = server.arg("u");
  String p = server.arg("p");
  String n = server.arg("n");
  int idx = findUser(u);
  if (idx < 0 || users[idx].pass != p || n.length() < 2) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  users[idx].pass = n;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleUserBlock() {
  String u = server.arg("u");
  String p = server.arg("p");
  String b = server.arg("b");
  int idx = findUser(u);
  if (idx < 0 || users[idx].pass != p || b.length() < 1) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  if (b == u) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  if (findUser(b) < 0) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  if (!userBlocks(users[idx], b)) {
    users[idx].blocked.push_back(b);
  }
  for (size_t i = 0; i < groups.size(); i++) {
    if (groups[i].owner == u) {
      for (size_t m = 0; m < groups[i].members.size(); m++) {
        if (groups[i].members[m] == b) {
          groups[i].members.erase(groups[i].members.begin() + m);
          break;
        }
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleRegister() {
  String u = server.arg("u");
  String p = server.arg("p");
  String ip = server.client().remoteIP().toString();
  if (ipBanned(ip)) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"ban\"}");
    return;
  }
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
  nu.ip = ip;
  nu.device = server.header("User-Agent");
  nu.online = false;
  nu.lastActiveMs = millis();
  nu.lastLoginMs = 0;
  users.push_back(nu);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleLogin() {
  String u = server.arg("u");
  String p = server.arg("p");
  String ip = server.client().remoteIP().toString();
  if (ipBanned(ip)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  if (isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":true,\"admin\":true}");
    return;
  }
  int idx = findUser(u);
  if (idx >= 0 && users[idx].pass == p) {
    users[idx].online = true;
    users[idx].ip = ip;
    users[idx].device = server.header("User-Agent");
    users[idx].lastActiveMs = millis();
    users[idx].lastLoginMs = millis();
    server.send(200, "application/json", "{\"ok\":true,\"admin\":false}");
  } else {
    server.send(200, "application/json", "{\"ok\":false}");
  }
}

void handleUsers() {
  String viewer = server.arg("u");
  String pass = server.arg("p");
  if (!isAdmin(viewer, pass)) {
    int v = findUser(viewer);
    if (v < 0 || users[v].pass != pass) {
      server.send(200, "application/json", "{\"users\":[]}");
      return;
    }
  }
  String out = "{\"users\":[";
  bool first = true;
  for (size_t i = 0; i < users.size(); i++) {
    updateOnline(users[i]);
    if (viewer.length() > 0 && viewer != users[i].name) {
      if (isBlockedPair(viewer, users[i].name)) continue;
    }
    unsigned long lastSec = (millis() - users[i].lastActiveMs) / 1000;
    if (!first) out += ",";
    first = false;
    out += "{\"name\":\"" + htmlEscape(users[i].name) + "\",\"online\":" + String(users[i].online ? "true" : "false") + ",\"ip\":\"" + users[i].ip + "\",\"last\":" + String(lastSec) + "}";
  }
  out += "]}";
  server.send(200, "application/json", out);
}

void handleGroups() {
  String viewer = server.arg("u");
  String pass = server.arg("p");
  if (!isAdmin(viewer, pass)) {
    int v = findUser(viewer);
    if (v < 0 || users[v].pass != pass) {
      server.send(200, "application/json", "{\"groups\":[]}");
      return;
    }
  }
  String out = "{\"groups\":[";
  bool first = true;
  for (size_t i = 0; i < groups.size(); i++) {
    bool isMember = false;
    for (size_t m = 0; m < groups[i].members.size(); m++) {
      if (groups[i].members[m] == viewer) {
        isMember = true;
        break;
      }
    }
    if (!isMember && !isAdmin(viewer, pass)) continue;
    if (viewer.length() > 0 && groups[i].owner.length() > 0) {
      if (isBlockedPair(viewer, groups[i].owner)) continue;
    }
    if (!first) out += ",";
    first = false;
    out += "{\"name\":\"" + htmlEscape(groups[i].name) + "\"}";
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

  if (!isG) {
    if (isBlockedPair(u, t)) {
      server.send(200, "application/json", "{\"ok\":false}");
      return;
    }
  } else {
    bool member = false;
    for (size_t i = 0; i < groups.size(); i++) {
      if (groups[i].name == t) {
        for (size_t m = 0; m < groups[i].members.size(); m++) {
          if (groups[i].members[m] == u) {
            member = true;
            break;
          }
        }
        break;
      }
    }
    if (!member && !isAdmin(u, p)) {
      server.send(200, "application/json", "{\"ok\":false}");
      return;
    }
  }

  Message msg;
  msg.from = u;
  msg.to = t;
  msg.text = m;
  msg.isGroup = isG;
  msg.ts = millis();
  messages.push_back(msg);

  int idx = findUser(u);
  if (idx >= 0) {
    users[idx].lastActiveMs = millis();
    users[idx].online = true;
  }

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
  if (!isG && isBlockedPair(u, t)) {
    server.send(200, "application/json", "{\"messages\":[]}");
    return;
  }
  if (isG && !isAdmin(u, p)) {
    bool member = false;
    for (size_t i = 0; i < groups.size(); i++) {
      if (groups[i].name == t) {
        for (size_t m = 0; m < groups[i].members.size(); m++) {
          if (groups[i].members[m] == u) {
            member = true;
            break;
          }
        }
        break;
      }
    }
    if (!member) {
      server.send(200, "application/json", "{\"messages\":[]}");
      return;
    }
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
  g.owner = u;
  g.members.clear();

  int start = 0;
  while (true) {
    int comma = members.indexOf(',', start);
    String part = (comma == -1) ? members.substring(start) : members.substring(start, comma);
    part.trim();
    if (part.length() > 0 && part != u) {
      if (findUser(part) >= 0 && !isBlockedPair(u, part)) {
        g.members.push_back(part);
      }
    }
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
    updateOnline(users[i]);
    unsigned long lastSec = (millis() - users[i].lastActiveMs) / 1000;
    out += "{\"name\":\"" + htmlEscape(users[i].name) + "\",\"pass\":\"" + htmlEscape(users[i].pass) + "\",\"ip\":\"" + users[i].ip + "\",\"online\":" + String(users[i].online ? "true" : "false") + ",\"last\":" + String(lastSec) + ",\"device\":\"" + htmlEscape(users[i].device) + "\"}";
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

void handleAdminStatus() {
  String u = server.arg("u");
  String p = server.arg("p");
  if (!isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  String ssid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "-";
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";
  long rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  int sta = WiFi.softAPgetStationNum();
  String out = "{\"ok\":true,\"ssid\":\"" + ssid + "\",\"ip\":\"" + ip + "\",\"rssi\":" + String(rssi) + ",\"sta\":" + String(sta) + "}";
  server.send(200, "application/json", out);
}

void handleAdminKick() {
  String u = server.arg("u");
  String p = server.arg("p");
  String t = server.arg("t");
  if (!isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  int idx = findUser(t);
  if (idx >= 0) {
    users[idx].online = false;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleAdminDelete() {
  String u = server.arg("u");
  String p = server.arg("p");
  String t = server.arg("t");
  if (!isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  int idx = findUser(t);
  if (idx >= 0) {
    users.erase(users.begin() + idx);
    for (size_t i = 0; i < groups.size(); i++) {
      for (size_t m = 0; m < groups[i].members.size(); ) {
        if (groups[i].members[m] == t) {
          groups[i].members.erase(groups[i].members.begin() + m);
        } else {
          m++;
        }
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleAdminReboot() {
  String u = server.arg("u");
  String p = server.arg("p");
  if (!isAdmin(u, p)) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
  delay(200);
  ESP.restart();
}

// -------------------- Setup/Loop --------------------
void setup() {
  Serial.begin(115200);
  loadWifiPrefs();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  const char *headerKeys[] = {"User-Agent"};
  size_t headerKeysSize = sizeof(headerKeys) / sizeof(char*);
  server.collectHeaders(headerKeys, headerKeysSize);

  server.on("/", handleRoot);
  server.on("/ping", handlePing);
  server.on("/logout", handleLogout);
  server.on("/user/updateName", handleUserUpdateName);
  server.on("/user/updatePass", handleUserUpdatePass);
  server.on("/user/block", handleUserBlock);
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
  server.on("/admin/status", handleAdminStatus);
  server.on("/admin/kick", handleAdminKick);
  server.on("/admin/delete", handleAdminDelete);
  server.on("/admin/reboot", handleAdminReboot);

  server.begin();
}

void loop() {
  server.handleClient();
}
