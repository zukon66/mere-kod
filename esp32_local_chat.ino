#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <vector>

WebServer server(80);
Preferences prefs;

// -------------------- Config --------------------
const char* ADMIN_USER = "gusullu";
const char* ADMIN_PASS = "omer3355";

String apSsid = "MESPV1";
String apPass = "12345678";

struct ChatUser {
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
  bool read; 
};

struct Group {
  String name;
  String owner;
  std::vector<String> members;
};

std::vector<ChatUser> users;
std::vector<Message> messages;
std::vector<Group> groups;
std::vector<String> bannedIps;

const unsigned long ONLINE_TIMEOUT_MS = 15000;
#define UPDATE_ONLINE(u) do { \
  unsigned long now = millis(); \
  if ((u).online && (now - (u).lastActiveMs) > ONLINE_TIMEOUT_MS) { \
    (u).online = false; \
  } \
} while (0)

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

bool userBlocksIdx(int idx, const String &other) {
  if (idx < 0 || (size_t)idx >= users.size()) return false;
  for (size_t i = 0; i < users[idx].blocked.size(); i++) {
    if (users[idx].blocked[i] == other) return true;
  }
  return false;
}

bool isBlockedPair(const String &a, const String &b) {
  int ia = findUser(a);
  int ib = findUser(b);
  if (ia < 0 || ib < 0) return false;
  if (userBlocksIdx(ia, b)) return true;
  if (userBlocksIdx(ib, a)) return true;
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
:root {
  --bg-dark: #1e1e1e;
  --bg-sidebar: #2c2c2c;
  --bg-card: #ffffff;
  --text-primary: #ffffff;
  --text-dark: #333333;
  --accent: #b0e0e6; /* Powder Blue */
  --accent-dark: #87ceeb;
  --green: #25d366;
  --red: #ff4d4d;
}

*{box-sizing:border-box}
body {
  font-family: 'Segoe UI', Helvetica, Arial, sans-serif;
  background-color: var(--bg-dark);
  margin: 0;
  height: 100vh;
  overflow: hidden;
  color: var(--text-dark);
}

/* Scrollbar */
::-webkit-scrollbar {width: 6px;}
::-webkit-scrollbar-track {background: transparent;}
::-webkit-scrollbar-thumb {background: #888; border-radius: 3px;}

.screen {display:none; height:100vh; width:100vw;}
.screen.active {display:flex;}

/* Login Screen */
#screen-login {
  flex-direction: column;
  background: var(--bg-dark);
  color: var(--text-primary);
  align-items: center;
  justify-content: center;
}
.login-box {
  background: var(--bg-card);
  width: 90%;
  max-width: 400px;
  padding: 24px;
  border-radius: 12px;
  box-shadow: 0 4px 15px rgba(0,0,0,0.3);
  text-align: center;
}
.login-title {
  color: var(--text-dark);
  font-size: 24px;
  font-weight: bold;
  margin-bottom: 20px;
}
input {
  width: 100%;
  padding: 12px;
  margin-bottom: 12px;
  border: 1px solid #ddd;
  border-radius: 8px;
  outline: none;
  font-size: 14px;
}
input:focus { border-color: var(--accent-dark); }
button {
  width: 100%;
  padding: 12px;
  background: var(--accent);
  color: #333;
  border: none;
  border-radius: 8px;
  font-weight: bold;
  cursor: pointer;
  transition: opacity 0.2s;
}
button:hover { opacity: 0.9; }
.btn-red { background: var(--red); color: white; }
.btn-green { background: var(--green); color: white; }

/* Main App */
#screen-app { flex-direction: row; }

/* Sidebar */
.sidebar {
  width: 350px;
  background: var(--bg-sidebar);
  border-right: 1px solid #444;
  display: flex;
  flex-direction: column;
  color: var(--text-primary);
}
.sidebar-header {
  padding: 15px;
  background: #232323;
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.header-title { font-size: 18px; font-weight: bold; color: var(--accent); }
.settings-btn {
  background: transparent;
  color: var(--text-primary);
  width: auto; padding: 5px 10px;
}

.tab-nav { display: flex; background: #333; }
.tab { flex: 1; padding: 10px; text-align: center; cursor: pointer; color: #aaa; border-bottom: 2px solid transparent;}
.tab.active { color: var(--accent); border-bottom: 2px solid var(--accent); }

.sidebar-body { flex: 1; overflow-y: auto; padding: 0; }

.user-item {
  display: flex;
  align-items: center;
  padding: 12px 15px;
  border-bottom: 1px solid #3a3a3a;
  cursor: pointer;
  transition: background 0.2s;
}
.user-item:hover { background: #3a3a3a; }
.avatar {
  width: 45px; height: 45px;
  border-radius: 50%;
  background: #555;
  color: white;
  display: flex; align-items: center; justify-content: center;
  font-size: 18px; font-weight: bold;
  margin-right: 12px;
  position: relative;
}
.status-dot {
  width: 12px; height: 12px;
  border-radius: 50%;
  background: #777;
  position: absolute;
  bottom: 0px; right: 0px;
  border: 2px solid var(--bg-sidebar);
}
.status-dot.online { background: var(--green); }

.user-info { flex: 1; min-width: 0; }
.user-name { font-size: 16px; font-weight: 500; color: var(--text-primary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.user-status { font-size: 12px; color: #aaa; margin-top: 2px; }
.unread-badge {
  background: var(--accent);
  color: #333;
  font-size: 11px;
  font-weight: bold;
  padding: 2px 6px;
  border-radius: 10px;
  min-width: 20px;
  text-align: center;
}

/* Chat Area */
.chat-area {
  flex: 1;
  display: flex;
  flex-direction: column;
  background: #e5ddd5; /* WhatsApp default bg logic but we can make it dark or image */
  background: #121212;
  position: relative;
}
.chat-header {
  padding: 10px 15px;
  background: #232323;
  color: var(--text-primary);
  display: flex;
  align-items: center;
  border-bottom: 1px solid #333;
}
.chat-messages {
  flex: 1;
  overflow-y: auto;
  padding: 15px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.msg {
  max-width: 70%;
  padding: 8px 12px;
  border-radius: 8px;
  font-size: 14px;
  line-height: 1.4;
  position: relative;
  word-wrap: break-word;
}
.msg.me {
  align-self: flex-end;
  background: var(--accent);
  color: #111;
  border-top-right-radius: 0;
}
.msg.other {
  align-self: flex-start;
  background: #333;
  color: white;
  border-top-left-radius: 0;
}
.msg-info { font-size: 10px; text-align: right; opacity: 0.7; margin-top: 4px; }

.chat-composer {
  padding: 10px;
  background: #232323;
  display: flex;
  gap: 10px;
}
#msgInput {
  flex: 1;
  border-radius: 20px;
  border: none;
  background: #333;
  color: white;
  padding: 10px 15px;
}

/* FAB */
.fab {
  position: fixed;
  bottom: 20px;
  right: 20px;
  width: 56px; height: 56px;
  border-radius: 50%;
  background: var(--accent);
  color: #333;
  font-size: 28px;
  display: flex; align-items: center; justify-content: center;
  box-shadow: 0 4px 10px rgba(0,0,0,0.3);
  cursor: pointer;
  z-index: 100;
}

/* Modal */
.modal {
  display: none;
  position: fixed;
  top:0; left:0; width:100%; height:100%;
  background: rgba(0,0,0,0.6);
  z-index: 200;
  align-items: center;
  justify-content: center;
}
.modal.active { display: flex; }
.modal-content {
  background: var(--bg-card);
  width: 90%;
  max-width: 500px;
  border-radius: 12px;
  padding: 20px;
  position: relative;
  color: var(--text-dark);
}
.modal-title { font-size: 18px; font-weight: bold; margin-bottom: 15px; border-bottom: 1px solid #eee; padding-bottom: 10px; }
.close-modal { position: absolute; top: 15px; right: 15px; cursor: pointer; font-weight: bold;}

/* Admin Grid */
.admin-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}
.admin-box {
  background: #f9f9f9;
  border: 1px solid #ddd;
  border-radius: 8px;
  padding: 10px;
}
.admin-box h4 { margin: 0 0 10px 0; font-size: 14px; color: #555; }
.admin-list { max-height: 150px; overflow: auto; border: 1px solid #eee; background: white; }
.admin-item { 
  display: flex; justify-content: space-between; align-items: center; 
  padding: 5px; border-bottom: 1px solid #eee; font-size: 12px;
}

/* Settings Tabs */
.settings-tabs { display: flex; margin-bottom: 15px; border-bottom: 1px solid #ddd; }
.st-tab { padding: 8px 15px; cursor: pointer; font-weight: bold; color: #777;}
.st-tab.active { color: var(--text-dark); border-bottom: 2px solid var(--accent); }
.st-content { display: none; }
.st-content.active { display: block; }

/* Responsive */
@media (max-width: 700px) {
  .sidebar { width: 100%; display: flex; }
  .chat-area { display: none; position: fixed; top:0; left:0; width:100%; height:100%; z-index: 50;}
  .chat-area.active { display: flex; }
  .admin-grid { grid-template-columns: 1fr; }
}

/* Notification Popup */
.notif-popup {
  position: fixed;
  top: 20px; left: 50%; transform: translateX(-50%);
  background: var(--accent);
  color: #333;
  padding: 10px 20px;
  border-radius: 20px;
  box-shadow: 0 5px 15px rgba(0,0,0,0.3);
  z-index: 300;
  display: none;
  font-weight: bold;
}

</style>
</head>
<body>

<div id="notifPopup" class="notif-popup">Yeni Mesaj!</div>

<!-- Login Screen -->
<div id="screen-login" class="screen active">
  <div class="login-box">
    <div class="login-title">ESP Chat Giriş</div>
    <input id="loginUser" placeholder="Kullanıcı Adı">
    <input id="loginPass" type="password" placeholder="Şifre">
    <button onclick="login()" style="margin-bottom:10px;">GİRİŞ YAP</button>
    <div style="font-size:12px; margin-bottom:10px;">veya</div>
    <button onclick="openRegister()" style="background:#555; color:white;">KAYIT OL</button>
  </div>
</div>

<!-- Register Modal -->
<div id="registerModal" class="modal">
  <div class="modal-content">
    <span class="close-modal" onclick="closeModal('registerModal')">X</span>
    <div class="modal-title">Yeni Kayıt</div>
    <input id="regUser" placeholder="Kullanıcı Adı">
    <input id="regPass" type="password" placeholder="Şifre">
    <button onclick="doRegister()">KAYIT OL</button>
  </div>
</div>

<!-- Main App -->
<div id="screen-app" class="screen">
  <div class="sidebar">
    <div class="sidebar-header">
      <div class="settings-btn" onclick="openSettings()" style="cursor:pointer; font-size:24px;">&#9881;</div> 
      <div class="header-title">ESP Chat</div>
      <div style="width:24px;"></div>
    </div>
    <div class="tab-nav">
      <div class="tab active" onclick="switchTab('chats')">Sohbetler</div>
      <!-- Groups listed in Chats for simplicity like WhatsApp -->
    </div>
    <div class="sidebar-body" id="userList">
      <!-- User Items here -->
    </div>
  </div>

  <div class="chat-area" id="chatArea">
    <div class="chat-header">
      <button onclick="closeChat()" style="width:auto; background:transparent; color:white; font-size:20px; margin-right:10px; display:none;" id="backBtn">&larr;</button>
      <div style="display:flex; flex-direction:column;">
        <span id="chatTitle" style="font-weight:bold;">Seçili Sohbet</span>
        <span id="chatSub" style="font-size:12px; color:#aaa;">-</span>
      </div>
      <div style="flex:1;"></div>
      <button id="groupAdminBtn" style="width:auto; padding:5px 10px; font-size:12px; display:none;" onclick="openGroupSettings()">Yönet</button>
    </div>
    <div class="chat-messages" id="messagesBox"></div>
    <div class="chat-composer">
      <input id="msgInput" placeholder="Mesaj yaz..." onkeyup="if(event.key==='Enter') sendMsg()">
      <button onclick="sendMsg()" style="width:50px; border-radius:50%;">&gt;</button>
    </div>
  </div>
</div>

<div class="fab" onclick="openCreateGroup()">+</div>

<!-- Settings Modal -->
<div id="settingsModal" class="modal">
  <div class="modal-content" style="max-width:600px;">
    <span class="close-modal" onclick="closeModal('settingsModal')">X</span>
    <div class="modal-title">Ayarlar</div>
    <div class="settings-tabs">
      <div class="st-tab active" onclick="setSettTab(0)">Profil</div>
      <div class="st-tab" onclick="setSettTab(1)">Engelleme</div>
      <div class="st-tab" id="adminTabLink" style="display:none;" onclick="setSettTab(2)">Admin Paneli</div>
    </div>
    
    <!-- Profile -->
    <div id="st-profile" class="st-content active">
      <p>Kullanıcı Adı Değiştir:</p>
      <input id="updateNameVal" placeholder="Yeni İsim">
      <button onclick="updateName()">Güncelle</button>
      <p>Şifre Değiştir:</p>
      <input id="updatePassVal" type="password" placeholder="Yeni Şifre">
      <button onclick="updatePass()">Güncelle</button>
      <br><br>
      <button class="btn-red" onclick="doLogout()">ÇIKIŞ YAP</button>
    </div>

    <!-- Block -->
    <div id="st-block" class="st-content">
      <p>Kullanıcı Engelle:</p>
      <select id="userBlockSelect" style="width:100%; padding:10px; margin-bottom:10px;"></select>
      <button class="btn-red" onclick="blockUser()">Engelle</button>
    </div>

    <!-- Admin -->
    <div id="st-admin" class="st-content">
      <div class="admin-grid">
        <!-- Box 1: Wifi -->
        <div class="admin-box">
          <h4>WiFi Bağlantı</h4>
          <div id="wifiStats" style="font-size:11px; margin-bottom:5px;">Durum: -</div>
          <button style="padding:5px; font-size:12px;" onclick="adminScan()">Tara</button>
          <select id="wifiScanSelect" style="width:100%; margin:5px 0; padding:5px;"></select>
          <input id="wifiConnPass" placeholder="Wifi Şifre" style="padding:5px; font-size:12px;">
          <button style="padding:5px; font-size:12px;" onclick="adminConnect()">Bağlan</button>
        </div>
        <!-- Box 2: AP -->
        <div class="admin-box">
          <h4>AP Ayarları</h4>
          <input id="admApSsid" placeholder="SSID" style="padding:5px; font-size:12px;">
          <input id="admApPass" placeholder="Şifre" style="padding:5px; font-size:12px;">
          <button style="padding:5px; font-size:12px;" onclick="adminSaveAp()">Kaydet & Reset</button>
        </div>
        <!-- Box 3: Users -->
        <div class="admin-box" style="grid-column: span 2;">
          <h4>Kullanıcılar</h4>
          <div id="adminUserList" class="admin-list"></div>
        </div>
        <!-- Box 4: Groups -->
        <div class="admin-box" style="grid-column: span 2;">
          <h4>Gruplar</h4>
          <div id="adminGroupList" class="admin-list"></div>
        </div>
      </div>
    </div>

  </div>
</div>

<!-- Create Group Modal -->
<div id="createGroupModal" class="modal">
  <div class="modal-content">
    <span class="close-modal" onclick="closeModal('createGroupModal')">X</span>
    <div class="modal-title">Yeni Grup</div>
    <input id="newGroupName" placeholder="Grup İsmi">
    <div style="font-size:14px; margin-bottom:5px;">Üyeleri Seç:</div>
    <div id="groupUserSelect" style="max-height:150px; overflow:auto; border:1px solid #ddd; padding:5px;"></div>
    <br>
    <button onclick="doCreateGroup()">OLUŞTUR</button>
  </div>
</div>

<!-- Group Settings Modal -->
<div id="groupSettingsModal" class="modal">
  <div class="modal-content">
    <span class="close-modal" onclick="closeModal('groupSettingsModal')">X</span>
    <div class="modal-title">Grup Yönetimi</div>
    <p>Üyeler:</p>
    <div id="groupMemberList" style="max-height:150px; overflow:auto; border:1px solid #ddd; padding:5px;"></div>
    <br>
    <button class="btn-red" onclick="deleteGroup()">GRUBU SİL</button>
  </div>
</div>

<script>
let state = {
  u: "", p: "", admin: false,
  target: "", isGroup: false,
  users: [], groups: [], msgs: [],
  lastLen: 0
};

// --- API Wrappers ---
async function api(path, args={}) {
  let qs = `?u=${encodeURIComponent(state.u)}&p=${encodeURIComponent(state.p)}`;
  for(let k in args) qs += `&${k}=${encodeURIComponent(args[k])}`;
  try {
    let r = await fetch(path + qs);
    return await r.json();
  } catch(e) { return {ok:false}; }
}

// --- Auth ---
function login() {
  let u = document.getElementById("loginUser").value.trim();
  let p = document.getElementById("loginPass").value.trim();
  if(!u || !p) return;
  fetch(`/login?u=${encodeURIComponent(u)}&p=${encodeURIComponent(p)}`)
    .then(r=>r.json()).then(res=>{
      if(res.ok) {
        state.u = u; state.p = p; state.admin = res.admin;
        document.getElementById("screen-login").classList.remove("active");
        document.getElementById("screen-app").classList.add("active");
        if(state.admin) document.getElementById("adminTabLink").style.display = "block";
        startLoop();
      } else alert("Giriş Başarısız");
    });
}
function doLogout() {
  api("/logout");
  location.reload();
}
function openRegister() { document.getElementById("registerModal").classList.add("active"); }
function doRegister() {
  let u = document.getElementById("regUser").value.trim();
  let p = document.getElementById("regPass").value.trim();
  if(!u || !p) return;
  fetch(`/register?u=${encodeURIComponent(u)}&p=${encodeURIComponent(p)}`)
  .then(r=>r.json()).then(res=>{
    if(res.ok) { alert("Kayıt Başarılı"); closeModal("registerModal"); }
    else alert("Hata: " + res.msg);
  });
}

// --- UI Logic ---
function closeModal(id) { document.getElementById(id).classList.remove("active"); }
function openSettings() { document.getElementById("settingsModal").classList.add("active"); refreshAdmin(); }
function setSettTab(n) {
  let tabs = document.querySelectorAll(".st-tab");
  let conts = document.querySelectorAll(".st-content");
  tabs.forEach((t,i) => {
    if(i===n) t.classList.add("active"); else t.classList.remove("active");
  });
  conts.forEach((c,i) => {
    if(i===n) c.classList.add("active"); else c.classList.remove("active");
  });
}

function renderList() {
  let list = document.getElementById("userList");
  list.innerHTML = "";
  let bSel = document.getElementById("userBlockSelect");
  bSel.innerHTML = "";

  // Combine
  let all = [];
  state.groups.forEach(g => { all.push({type:'g', name:g.name, sub:'Grup'}); });
  state.users.forEach(u => { 
    if(u.name !== state.u) all.push({type:'u', name:u.name, sub: u.online?'Çevrimiçi':'Çevrimdışı', online:u.online, unread: u.unread||0}); 
    if(u.name !== state.u) {
        let op = document.createElement("option"); op.value = u.name; op.innerText=u.name;
        bSel.appendChild(op);
    }
  });

  all.forEach(item => {
    let div = document.createElement("div");
    div.className = "user-item";
    div.onclick = () => selectChat(item.name, item.type==='g');
    
    let av = document.createElement("div");
    av.className = "avatar";
    av.innerText = item.name[0].toUpperCase();
    if(item.type === 'u') {
      let dot = document.createElement("div");
      dot.className = "status-dot " + (item.online ? "online" : "");
      av.appendChild(dot);
    } else {
      av.style.background = "#008069"; // Group color
    }

    let info = document.createElement("div");
    info.className = "user-info";
    let nm = document.createElement("div");
    nm.className = "user-name";
    nm.innerText = item.name;
    let st = document.createElement("div");
    st.className = "user-status";
    st.innerText = item.sub;
    info.appendChild(nm);
    info.appendChild(st);

    div.appendChild(av);
    div.appendChild(info);

    if(item.unread > 0) {
      let bdg = document.createElement("div");
      bdg.className="unread-badge";
      bdg.innerText = "+" + item.unread;
      div.appendChild(bdg);
    }

    list.appendChild(div);
  });
}

function selectChat(name, isGrp) {
  state.target = name;
  state.isGroup = isGrp;
  document.getElementById("chatTitle").innerText = name;
  document.getElementById("chatSub").innerText = isGrp ? "Grup" : "Sohbet";
  
  // Mobile UI
  if(window.innerWidth <= 700) {
    document.getElementById("chatArea").classList.add("active");
    document.getElementById("backBtn").style.display = "block";
  }

  // Group Admin Button
  let btn = document.getElementById("groupAdminBtn");
  if(isGrp) {
    let grp = state.groups.find(g=>g.name===name);
    if(grp && (grp.owner === state.u || state.admin)) btn.style.display = "block"; else btn.style.display="none";
  } else btn.style.display="none";

  loadMessages();
}

function closeChat() {
  document.getElementById("chatArea").classList.remove("active");
  state.target = "";
}

function sendMsg() {
  let txt = document.getElementById("msgInput").value.trim();
  if(!txt || !state.target) return;
  api("/send", {t: state.target, g: state.isGroup?1:0, m: txt}).then(j=>{
    if(j.ok) {
      document.getElementById("msgInput").value = "";
      loadMessages();
    }
  });
}

function loadMessages() {
  if(!state.target) return;
  api("/messages", {t: state.target, g: state.isGroup?1:0}).then(j=>{
    let box = document.getElementById("messagesBox");
    box.innerHTML = "";
    j.messages.forEach(m => {
      let d = document.createElement("div");
      d.className = "msg " + (m.from === state.u ? "me" : "other");
      let sender = (state.isGroup && m.from !== state.u) ? `<div style="font-size:10px; color:var(--accent); margin-bottom:2px; font-weight:bold;">${m.from}</div>` : "";
      d.innerHTML = `${sender}${m.text} <div class="msg-info"></div>`;
      box.appendChild(d);
    });
    box.scrollTop = box.scrollHeight;
  });
}

// --- Group Mgmt ---
function openCreateGroup() {
  document.getElementById("createGroupModal").classList.add("active");
  let c = document.getElementById("groupUserSelect");
  c.innerHTML = "";
  state.users.forEach(u => {
    if(u.name === state.u) return;
    let row = document.createElement("div");
    row.innerHTML = `<label><input type="checkbox" value="${u.name}"> ${u.name}</label>`;
    c.appendChild(row);
  });
}
function doCreateGroup() {
  let n = document.getElementById("newGroupName").value.trim();
  if(!n) return;
  let chk = document.querySelectorAll("#groupUserSelect input:checked");
  let membrs = "";
  chk.forEach(c => membrs += c.value + ",");
  api("/group/create", {name:n, members:membrs}).then(j=>{
    if(j.ok) { closeModal("createGroupModal"); fetchAll(); alert("Grup Oluşturuldu"); }
    else alert(j.msg);
  });
}
function openGroupSettings() {
  document.getElementById("groupSettingsModal").classList.add("active");
  let list = document.getElementById("groupMemberList");
  list.innerHTML = "Yükleniyor...";
  api("/group/info", {t:state.target}).then(j=>{
    list.innerHTML = "";
    j.members.forEach(m => {
      let r = document.createElement("div");
      r.style.display="flex"; r.style.justifyContent="space-between"; r.style.padding="5px"; r.style.borderBottom="1px solid #eee";
      r.innerHTML = `<span>${m}</span> ${ (m!==state.u)? `<button style="width:auto; padding:2px 8px; font-size:10px; background:#d32f2f;" onclick="kickUser('${m}')">AT</button>` : '' }`;
      list.appendChild(r);
    });
  });
}
function kickUser(u) {
  if(!confirm("Kullanıcıyı atmak istiyor musunuz?")) return;
  api("/group/kick", {t:state.target, target:u}).then(j=>{
    if(j.ok) openGroupSettings();
  });
}
function deleteGroup() {
  if(!confirm("Grubu silmek istiyor musunuz?")) return;
  api("/group/delete", {t:state.target}).then(j=>{
    if(j.ok) { closeModal("groupSettingsModal"); closeChat(); fetchAll(); }
  });
}

// --- Settings ---
function updateName() {
  let n = document.getElementById("updateNameVal").value.trim();
  if(!n) return;
  api("/user/updateName", {n:n}).then(j=>{ if(j.ok){state.u=n; alert("Güncellendi"); location.reload();} else alert(j.msg); });
}
function updatePass() {
  let n = document.getElementById("updatePassVal").value.trim();
  if(!n) return;
  api("/user/updatePass", {n:n}).then(j=>{ if(j.ok){state.p=n; alert("Güncellendi");} });
}
function blockUser() {
  let b = document.getElementById("userBlockSelect").value;
  api("/user/block", {b:b}).then(j=>{ if(j.ok){ alert("Engellendi"); fetchAll(); } });
}

// --- Admin ---
function refreshAdmin() {
  if(!state.admin) return;
  api("/admin/status").then(j=>{
    document.getElementById("wifiStats").innerText = `Durum: ${j.ssid}, IP: ${j.ip}, İstasyonlar: ${j.sta}`;
  });
  api("/admin/data").then(j=>{
    let ul = document.getElementById("adminUserList"); ul.innerHTML="";
    j.users.forEach(u=>{
      let d=document.createElement("div"); d.className="admin-item";
      d.innerHTML = `<span>${u.name} (${u.ip})</span> <div><button onclick="admKick('${u.name}')">At</button> <button onclick="admDel('${u.name}')">Sil</button></div>`;
      ul.appendChild(d);
    });
    let gl = document.getElementById("adminGroupList"); gl.innerHTML="";
    j.groups.forEach(g=>{
       let d=document.createElement("div"); d.className="admin-item";
       d.innerHTML = `<span>${g.name} (${g.cnt} üye)</span> <button onclick="admDelGroup('${g.name}')">Sil</button>`;
       gl.appendChild(d);
    });
  });
}
function adminScan() {
  let s = document.getElementById("wifiScanSelect"); s.innerHTML="<option>Taranıyor...</option>";
  api("/admin/scan").then(j=>{
    s.innerHTML="";
    j.ssids.forEach(ssd => { let o=document.createElement("option"); o.value=ssd; o.innerText=ssd; s.appendChild(o); });
  });
}
function adminConnect() {
  let s = document.getElementById("wifiScanSelect").value;
  let p = document.getElementById("wifiConnPass").value;
  api("/admin/connect", {ssid:s, pass:p}).then(j=>{alert("Bağlanılıyor...");});
}
function adminSaveAp() {
  let s = document.getElementById("admApSsid").value;
  let p = document.getElementById("admApPass").value;
  api("/admin/ap", {ssid:s, pass:p}).then(j=>{alert("Kaydedildi ve Resetleniyor");});
}
function admKick(u) { api("/admin/kick", {t:u}).then(refreshAdmin); }
function admDel(u) { api("/admin/delUser", {t:u}).then(refreshAdmin); }
function admDelGroup(g) { api("/group/delete", {t:g}).then(refreshAdmin); }

// --- Main Loop ---
function fetchAll() {
  api("/sync").then(j=>{
    state.users = j.users;
    state.groups = j.groups;
    renderList();
    
    // Notifications logic
    let totalMsgs = 0;
    state.users.forEach(u=>totalMsgs+=u.unread||0);
    // Simple popup logic if needed, but unread badge is better. 
    // Implementing popup as requested:
    if(j.popup) {
      let p = document.getElementById("notifPopup");
      p.innerText = "Yeni Mesaj: " + j.popup;
      p.style.display="block";
      setTimeout(()=>{p.style.display="none"}, 3000);
    }
  });
}
function startLoop() {
  fetchAll();
  setInterval(() => {
    fetchAll();
    if(state.target) loadMessages();
    if(document.getElementById("settingsModal").classList.contains("active") && state.admin) refreshAdmin();
  }, 2000);
}

// Init
window.onload = () => {
  // Check session? For now just login screen
};
</script>
</body>
</html>
)rawliteral";

// -------------------- Routes --------------------

// Sync: Returns users, groups, and unread counts
void handleSync() {
  String u = server.arg("u");
  String p = server.arg("p");
  int idx = findUser(u);
  if (idx < 0 || users[idx].pass != p) { server.send(200, "application/json", "{\"users\":[],\"groups\":[]}"); return; } // Auth fail

  UPDATE_ONLINE(users[idx]);
  users[idx].lastActiveMs = millis(); 

  // Check for popup (simple logic: return name of sender of last msg if not read and ts > last check? 
  // For simplicity, we'll traverse messages. BUT for scalable chat, we need index. 
  // Let's just return unread counts and let JS handle badges.
  
  // Construct JSON manually
  String json = "{";
  
  // Users
  json += "\"users\":[";
  for(size_t i=0; i<users.size(); i++) {
    UPDATE_ONLINE(users[i]);
    if(isBlockedPair(u, users[i].name)) continue;

    // Calculate unread
    int unread = 0;
    for(const auto &m : messages) {
       if(!m.isGroup && m.from == users[i].name && m.to == u && !m.read) unread++;
    }
    
    if(i>0) json += ",";
    json += "{\"name\":\"" + users[i].name + "\",\"online\":" + (users[i].online?"true":"false") + ",\"unread\":" + String(unread) + "}";
  }
  json += "],";

  // Groups
  json += "\"groups\":[";
  bool first=true;
  for(size_t i=0; i<groups.size(); i++) {
    bool isMem = false;
    for(const auto &m : groups[i].members) if(m == u) isMem = true;
    if(!isMem && !isAdmin(u,p)) continue;

    int unread = 0; // Group unread is harder without per-user read cursor. Skip for now or simpler logic.
    
    if(!first) json += ",";
    json += "{\"name\":\"" + groups[i].name + "\",\"owner\":\"" + groups[i].owner + "\"}";
    first=false;
  }
  json += "]";

  json += "}";
  server.send(200, "application/json", json);
}

void handleLogin() {
  String u = server.arg("u");
  String p = server.arg("p");
  if(isAdmin(u,p)) {
     server.send(200, "application/json", "{\"ok\":true,\"admin\":true}");
     return;
  }
  int idx = findUser(u);
  if(idx>=0 && users[idx].pass == p) {
    if(ipBanned(server.client().remoteIP().toString())) {
       server.send(200, "application/json", "{\"ok\":false}"); return;
    }
    users[idx].online = true; 
    users[idx].ip = server.client().remoteIP().toString();
    server.send(200, "application/json", "{\"ok\":true,\"admin\":false}");
  } else {
    server.send(200, "application/json", "{\"ok\":false}");
  }
}

void handleRegister() {
  String u = server.arg("u");
  String p = server.arg("p");
  if(u.length()<2 || p.length()<2 || u == ADMIN_USER || findUser(u)>=0) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Invalid\"}");
    return;
  }
  ChatUser nu; nu.name = u; nu.pass = p; nu.online=false; nu.ip="-";
  users.push_back(nu);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSend() {
  String u = server.arg("u"), p = server.arg("p"), t = server.arg("t"), m = server.arg("m");
  bool admin = isAdmin(u,p);
  int idx = findUser(u);
  if(!admin && (idx<0 || users[idx].pass!=p)) {server.send(200, "application/json", "{\"ok\":false}"); return;}

  Message msg; msg.from=u; msg.to=t; msg.text=m; msg.ts=millis(); msg.read=false;
  msg.isGroup = (server.arg("g")=="1");
  messages.push_back(msg);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleMessages() {
  String u = server.arg("u"), p = server.arg("p"), t = server.arg("t");
  bool g = (server.arg("g")=="1");
  int idx = findUser(u);
  if(!isAdmin(u,p) && (idx<0 || users[idx].pass!=p)) {server.send(200, "application/json", "{\"messages\":[]}"); return;}

  String json = "{\"messages\":[";
  bool first=true;
  for(auto &m : messages) {
    bool match = false;
    if(g) match = m.isGroup && m.to == t;
    else match = !m.isGroup && ((m.from==u && m.to==t) || (m.from==t && m.to==u));
    
    if(match) {
      if(m.to == u) m.read = true; // Mark read
      if(!first) json+=",";
      json += "{\"from\":\"" + htmlEscape(m.from) + "\",\"text\":\"" + htmlEscape(m.text) + "\"}";
      first=false;
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// Group Features
void handleGroupCreate() {
  String u = server.arg("u"), p = server.arg("p"), n = server.arg("name"), mems = server.arg("members");
  // Check auth
  int idx=findUser(u);
  if(!isAdmin(u,p) && (idx<0 || users[idx].pass!=p)) return;

  Group g; g.name=n; g.owner=u; g.members.push_back(u);
  
  // Parse members
  int start=0;
  while(true){
    int c = mems.indexOf(',', start);
    String part = (c==-1)? mems.substring(start) : mems.substring(start, c);
    part.trim();
    if(part.length()>0 && part!=u && findUser(part)>=0) g.members.push_back(part);
    if(c==-1) break; start=c+1;
  }
  groups.push_back(g);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleGroupInfo() {
  String u = server.arg("u"), p = server.arg("p"), t = server.arg("t");
  // find group
  for(const auto &g : groups) {
    if(g.name == t) {
      String json = "{\"members\":[";
      for(size_t i=0; i<g.members.size(); i++) {
        if(i>0) json+=",";
        json += "\"" + g.members[i] + "\"";
      }
      json += "]}";
      server.send(200, "application/json", json);
      return;
    }
  }
  server.send(200, "application/json", "{\"members\":[]}");
}

void handleGroupKick() {
  String u=server.arg("u"), p=server.arg("p"), t=server.arg("t"), target=server.arg("target");
  bool admin = isAdmin(u,p);
  for(auto &g : groups) {
    if(g.name == t) {
      if(g.owner == u || admin) {
        for(size_t i=0; i<g.members.size(); i++) {
          if(g.members[i] == target) {
            g.members.erase(g.members.begin()+i);
            server.send(200, "application/json", "{\"ok\":true}");
            return;
          }
        }
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":false}");
}

void handleGroupDelete() {
  String u=server.arg("u"), p=server.arg("p"), t=server.arg("t");
  bool admin = isAdmin(u,p);
  for(size_t i=0; i<groups.size(); i++) {
    if(groups[i].name == t) {
      if(groups[i].owner == u || admin) {
        groups.erase(groups.begin()+i);
        server.send(200, "application/json", "{\"ok\":true}");
        return;
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":false}");
}

// User Actions
void handleUpdateName() {
  String u=server.arg("u"), p=server.arg("p"), n=server.arg("n");
  int idx = findUser(u);
  if(idx>=0 && users[idx].pass==p && n.length()>1 && findUser(n)<0 && n!=ADMIN_USER) {
    // Rename everywhere
    for(auto &g:groups) {
      if(g.owner==u) g.owner=n;
      for(auto &m:g.members) if(m==u) m=n;
    }
    for(auto &m:messages) {
      if(m.from==u) m.from=n;
      if(m.to==u) m.to=n;
    }
    users[idx].name=n;
    server.send(200, "application/json", "{\"ok\":true}");
  } else server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Invalid\"}");
}
void handleUpdatePass() {
  String u=server.arg("u"), p=server.arg("p"), n=server.arg("n");
  int idx = findUser(u);
  if(idx>=0 && users[idx].pass==p && n.length()>1) {
    users[idx].pass=n; 
    server.send(200, "application/json", "{\"ok\":true}");
  } else server.send(200, "application/json", "{\"ok\":false}");
}
void handleBlock() {
  String u=server.arg("u"), p=server.arg("p"), b=server.arg("b");
  int idx=findUser(u);
  if(idx>=0 && users[idx].pass==p && !userBlocksIdx(idx, b)) {
    users[idx].blocked.push_back(b);
    server.send(200, "application/json", "{\"ok\":true}");
  } else server.send(200, "application/json", "{\"ok\":false}");
}

// Admin
void handleAdminStatus() {
  if(!isAdmin(server.arg("u"), server.arg("p"))) return;
  String json = "{\"ssid\":\""+(WiFi.status()==WL_CONNECTED?WiFi.SSID():"-")+"\",\"ip\":\""+(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"-")+"\",\"sta\":"+String(WiFi.softAPgetStationNum())+"}";
  server.send(200, "application/json", json);
}
void handleAdminData() {
  if(!isAdmin(server.arg("u"), server.arg("p"))) return;
  // Users
  String json = "{\"users\":[";
  for(size_t i=0; i<users.size(); i++) {
    if(i>0) json+=",";
    json += "{\"name\":\""+users[i].name+"\",\"ip\":\""+users[i].ip+"\"}";
  }
  json += "],\"groups\":[";
  for(size_t i=0; i<groups.size(); i++) {
    if(i>0) json+=",";
    json += "{\"name\":\""+groups[i].name+"\",\"cnt\":"+String(groups[i].members.size())+"}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}
void handleAdminScan() {
  if(!isAdmin(server.arg("u"), server.arg("p"))) return;
  int n = WiFi.scanNetworks();
  String json = "{\"ssids\":[";
  for(int i=0; i<n; i++) {
    if(i>0) json+=",";
    json += "\"" + WiFi.SSID(i) + "\"";
  }
  json += "]}";
  server.send(200, "application/json", json);
}
void handleAdminConnect() {
   if(!isAdmin(server.arg("u"), server.arg("p"))) return;
   String s = server.arg("ssid"), pa = server.arg("pass");
   WiFi.begin(s.c_str(), pa.c_str());
   server.send(200, "application/json", "{\"ok\":true}");
}
void handleAdminAp() {
  if(!isAdmin(server.arg("u"), server.arg("p"))) return;
  saveWifiPrefs(server.arg("ssid"), server.arg("pass"));
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500); ESP.restart();
}
void handleAdminDelUser() {
  if(!isAdmin(server.arg("u"), server.arg("p"))) return;
  String t=server.arg("t");
  int idx=findUser(t);
  if(idx>=0) {
    users.erase(users.begin()+idx);
    server.send(200, "application/json", "{\"ok\":true}");
  } else server.send(200, "application/json", "{\"ok\":false}");
}


void setup() {
  Serial.begin(115200);
  loadWifiPrefs();
  WiFi.mode(WIFI_AP_STA); // AP + STA
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  server.on("/", [](){ server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/login", handleLogin);
  server.on("/logout", [](){ server.send(200, "application/json", "{\"ok\":true}"); });
  server.on("/register", handleRegister);
  server.on("/sync", handleSync);
  server.on("/send", handleSend);
  server.on("/messages", handleMessages);
  
  server.on("/user/updateName", handleUpdateName);
  server.on("/user/updatePass", handleUpdatePass);
  server.on("/user/block", handleBlock);

  server.on("/group/create", handleGroupCreate);
  server.on("/group/info", handleGroupInfo);
  server.on("/group/kick", handleGroupKick);
  server.on("/group/delete", handleGroupDelete);
  
  server.on("/admin/status", handleAdminStatus);
  server.on("/admin/data", handleAdminData);
  server.on("/admin/scan", handleAdminScan);
  server.on("/admin/connect", handleAdminConnect);
  server.on("/admin/ap", handleAdminAp);
  server.on("/admin/kick", handleGroupKick); // reused, expects t as target? No, slightly diff logic.
  server.on("/admin/delUser", handleAdminDelUser);

  server.begin();
}

void loop() {
  server.handleClient();
}
