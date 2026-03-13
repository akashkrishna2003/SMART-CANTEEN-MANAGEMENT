/*
 * Smart Canteen Management System - ESP-01 Web Server
 * Handles WiFi connectivity, Web Interface, User/Menu Management, Kitchen Panel
 * 
 * Communication with Arduino via Serial (TX/RX)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <time.h>

// WiFi Credentials - Change these!
const char* ssid = "Redmi Note 14 5G";
const char* password = "9876543210";

// Static IP Configuration - Change these for your network!
// Set USE_STATIC_IP to true to use fixed IP
#define USE_STATIC_IP true

IPAddress staticIP(10, 29, 113, 100);      // ESP will always use this IP
IPAddress gateway(10, 29, 113, 1);         // Your router/hotspot IP
IPAddress subnet(255, 255, 255, 0);        // Subnet mask
IPAddress dns(8, 8, 8, 8);                 // Google DNS

// NTP Time Settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;  // IST = UTC+5:30 = 19800 seconds
const int daylightOffset_sec = 0;

// Web Server
ESP8266WebServer server(80);

// Data Structures
#define MAX_USERS 20
#define MAX_MENU_ITEMS 20
#define MAX_ORDERS 10
#define MAX_CART_ITEMS 10
#define MAX_STOCK_ALERTS 10
#define LOW_STOCK_THRESHOLD 10

struct User {
  char uid[20];
  char name[20];
  float balance;
  float creditUsed;
  float creditLimit;
  bool active;
};

struct MenuItem {
  char key[4];
  char name[20];
  float price;
  int stock;
  bool active;
};

struct CartItem {
  char key[4];
  int qty;
};

struct Order {
  int id;
  char uid[20];
  char userName[20];
  CartItem items[MAX_CART_ITEMS];
  int itemCount;
  float total;
  char status[10];  // "pending", "ready", "cancelled"
  char timeStr[20]; // "DD/MM HH:MM"
  unsigned long timestamp;
  bool active;
};

// Global Data Arrays
User users[MAX_USERS];
int userCount = 0;

MenuItem menuItems[MAX_MENU_ITEMS];
int menuItemCount = 0;

Order orders[MAX_ORDERS];
int orderCount = 0;
int nextOrderId = 1;

// Online Users Tracking
char onlineUsers[MAX_USERS][20];
unsigned long onlineTimestamps[MAX_USERS];
int onlineCount = 0;

// Stock Alerts Tracking
struct StockAlert {
  char key[4];
  char name[20];
  int stock;
  unsigned long timestamp;
  bool acknowledged;
};
StockAlert stockAlerts[MAX_STOCK_ALERTS];
int stockAlertCount = 0;

// Transaction History
#define MAX_TRANSACTIONS 50
struct Transaction {
  int orderId;
  char uid[20];
  char userName[20];
  float amount;
  char type[10];  // "order", "recharge", "refund"
  char timeStr[20]; // "DD/MM HH:MM"
  unsigned long timestamp;
  bool active;
};
Transaction transactions[MAX_TRANSACTIONS];
int transactionCount = 0;

// Sales Analytics
float dailySales[7] = {0};  // Last 7 days
int dailyOrders[7] = {0};
int itemSales[MAX_MENU_ITEMS] = {0};  // Track qty sold per item

// System Lock Status
bool systemLocked = false;
char lockMessage[32] = "System Closed";

// Admin Credentials
const char* adminUser = "admin";
const char* adminPass = "canteen123";

// EEPROM Addresses
#define EEPROM_SIZE 4096
#define USER_START_ADDR 0
#define MENU_START_ADDR 1000
#define CONFIG_ADDR 3500

// Lightweight HTML - Minified for ESP-01
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Canteen</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}body{font:14px sans-serif;min-height:100vh;padding:12px;background:#0f0c29;overflow-x:hidden}
.bg{position:fixed;top:0;left:0;width:100%;height:100%;background:linear-gradient(-45deg,#0f0c29,#302b63,#24243e,#667eea);background-size:400% 400%;animation:gbg 15s ease infinite;z-index:-2}
@keyframes gbg{0%,100%{background-position:0% 50%}50%{background-position:100% 50%}}
.particles{position:fixed;top:0;left:0;width:100%;height:100%;z-index:-1;overflow:hidden}
.particles span{position:absolute;display:block;width:20px;height:20px;background:rgba(255,255,255,.1);animation:float 25s linear infinite;bottom:-150px}
.particles span:nth-child(1){left:25%;width:80px;height:80px;animation-delay:0s}
.particles span:nth-child(2){left:10%;width:20px;height:20px;animation-delay:2s;animation-duration:12s}
.particles span:nth-child(3){left:70%;width:20px;height:20px;animation-delay:4s}
.particles span:nth-child(4){left:40%;width:60px;height:60px;animation-delay:0s;animation-duration:18s}
.particles span:nth-child(5){left:65%;width:20px;height:20px;animation-delay:0s}
.particles span:nth-child(6){left:75%;width:110px;height:110px;animation-delay:3s}
.particles span:nth-child(7){left:35%;width:150px;height:150px;animation-delay:7s}
.particles span:nth-child(8){left:50%;width:25px;height:25px;animation-delay:15s;animation-duration:45s}
.particles span:nth-child(9){left:20%;width:15px;height:15px;animation-delay:2s;animation-duration:35s}
.particles span:nth-child(10){left:85%;width:150px;height:150px;animation-delay:0s;animation-duration:11s}
@keyframes float{0%{transform:translateY(0) rotate(0deg);opacity:1;border-radius:0}100%{transform:translateY(-1000px) rotate(720deg);opacity:0;border-radius:50%}}
.login{position:fixed;top:0;left:0;width:100%;height:100%;display:flex;justify-content:center;align-items:center;z-index:1000}
.login-box{background:rgba(255,255,255,.95);padding:40px;border-radius:20px;box-shadow:0 25px 50px rgba(0,0,0,.5);text-align:center;max-width:360px;width:90%;backdrop-filter:blur(10px)}
.login-box h2{color:#667eea;margin-bottom:8px;font-size:1.8em}
.login-box p{color:#666;margin-bottom:24px}
.login-box input{width:100%;padding:14px;margin:8px 0;border:2px solid #e0e0e0;border-radius:10px;font-size:15px}
.login-box input:focus{outline:0;border-color:#667eea;box-shadow:0 0 0 3px rgba(102,126,234,.2)}
.login-box button{width:100%;padding:14px;background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;border:0;border-radius:10px;font-size:16px;font-weight:600;cursor:pointer;margin-top:12px;transition:transform .2s,box-shadow .2s}
.login-box button:hover{transform:translateY(-2px);box-shadow:0 8px 20px rgba(102,126,234,.4)}
.login-err{color:#eb3349;font-size:13px;margin-top:10px;display:none}
.w{max-width:850px;margin:auto;background:#fff;padding:16px;border-radius:12px;box-shadow:0 8px 32px rgba(0,0,0,.2)}
.hd{background:linear-gradient(90deg,#667eea,#764ba2);color:#fff;padding:12px 16px;margin:-16px -16px 16px;border-radius:12px 12px 0 0;text-align:center}
.hd h1{font-size:1.4em;margin:0}.hd span{font-size:.85em;opacity:.9}
.tabs{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:14px;background:#f0f0f0;padding:6px;border-radius:8px}
.t{flex:1;padding:10px 16px;background:transparent;color:#555;border:0;border-radius:6px;cursor:pointer;font-weight:600;transition:all .2s}
.t:hover{background:#e0e0e0}.t.a{background:linear-gradient(90deg,#667eea,#764ba2);color:#fff;box-shadow:0 2px 8px rgba(102,126,234,.4)}
.p{display:none}.p.a{display:block}
.stats{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:12px}
.stat{flex:1;min-width:80px;background:linear-gradient(135deg,#f5f7fa,#e4e8eb);padding:12px;border-radius:8px;text-align:center}
.stat b{display:block;font-size:1.5em;color:#667eea}.stat span{font-size:.75em;color:#666;text-transform:uppercase}
table{width:100%;border-collapse:collapse;font-size:13px;margin-top:10px}th,td{padding:8px;border-bottom:1px solid #eee;text-align:left}th{background:linear-gradient(90deg,#f8f9fa,#e9ecef);color:#555;font-weight:600}
tr:hover{background:#f8f9ff}
.b{padding:6px 12px;border:0;border-radius:6px;cursor:pointer;margin:2px;font-weight:500;transition:all .2s;box-shadow:0 2px 4px rgba(0,0,0,.1)}
.b:hover{transform:translateY(-1px);box-shadow:0 4px 8px rgba(0,0,0,.15)}.bg{background:linear-gradient(135deg,#56ab2f,#a8e063);color:#fff}.br{background:linear-gradient(135deg,#eb3349,#f45c43);color:#fff}.by{background:linear-gradient(135deg,#f7971e,#ffd200);color:#333}
.bp{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff}.bb{background:linear-gradient(135deg,#00b4db,#0083b0);color:#fff}.bk{background:#555;color:#fff}
.m{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.6);justify-content:center;align-items:center;backdrop-filter:blur(4px)}.m.s{display:flex}
.mc{background:#fff;padding:20px;border-radius:12px;width:90%;max-width:360px;box-shadow:0 16px 48px rgba(0,0,0,.3)}
.mc b{color:#667eea;font-size:1.1em}input{width:100%;padding:10px;margin:6px 0 12px;border:2px solid #e0e0e0;border-radius:8px;transition:border .2s}input:focus{outline:0;border-color:#667eea}
.o{background:#fff;padding:14px;margin:10px 0;border-radius:10px;border-left:4px solid #667eea;box-shadow:0 2px 8px rgba(0,0,0,.08)}.o.r{border-color:#56ab2f;background:#f8fff8}.o.c{border-color:#eb3349;opacity:.6}
.st{display:inline-block;padding:3px 10px;border-radius:12px;font-size:11px;font-weight:600;margin-left:8px}.sp{background:#fff3cd;color:#856404}.sr{background:#d4edda;color:#155724}.sc{background:#f8d7da;color:#721c24}
</style></head><body>
<div class="bg"></div>
<div class="particles"><span></span><span></span><span></span><span></span><span></span><span></span><span></span><span></span><span></span><span></span></div>
<div id="loginScreen" class="login"><div class="login-box"><h2>&#127860; Smart Canteen</h2><p>Admin Login</p><input id="auser" placeholder="&#128100; Username" autocomplete="off"><input id="apass" type="password" placeholder="&#128274; Password"><button onclick="doLogin()">&#128272; Login</button><div id="lerr" class="login-err">Invalid credentials</div></div></div>
<div id="mainApp" style="display:none"><div class="w">
<div class="hd"><div style="display:flex;justify-content:space-between;align-items:center"><div><h1 style="text-align:left">&#127860; Smart Canteen</h1><span>Management System</span></div><button class="b br" onclick="doLogout()" style="padding:8px 16px">&#128682; Logout</button></div><div style="margin-top:8px;padding:6px 10px;background:rgba(255,255,255,.2);border-radius:6px;font-size:12px">&#127760; IP: <b id="ipaddr">--</b> (Bookmark this!)</div></div>
<div class="tabs"><button class="t a" onclick="sw(0)">&#128202; Dashboard</button><button class="t" onclick="sw(1)">&#128100; Users</button><button class="t" onclick="sw(2)">&#127828; Menu</button><button class="t" onclick="sw(3)">&#128293; Kitchen</button><button class="t" onclick="sw(4)">&#128200; Reports</button></div>
<div id="p0" class="p a"><div id="lockBanner" style="display:none;background:linear-gradient(135deg,#ff6b6b,#ee5a5a);color:#fff;padding:14px;border-radius:10px;margin-bottom:12px;text-align:center"><b>&#128274; SYSTEM LOCKED</b><br><span id="lockMsg">System Closed</span></div><div style="background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;padding:12px 16px;border-radius:10px;margin-bottom:12px;display:flex;justify-content:space-between;align-items:center"><div><b id="cdate">--</b></div><div style="font-size:1.5em;font-weight:bold" id="ctime">--:--:--</div></div><div class="stats"><div class="stat"><b id="tu">0</b><span>Users</span></div><div class="stat"><b id="tm">0</b><span>Menu Items</span></div><div class="stat"><b id="to">0</b><span>Pending</span></div><div class="stat"><b id="on">0</b><span>Online</span></div><div class="stat" id="sas"><b id="sa">0</b><span>Low Stock</span></div></div><div style="display:flex;gap:8px;flex-wrap:wrap;margin-top:10px"><button class="b by" onclick="rf()">&#8635; Refresh</button><button id="lockBtn" class="b bg" onclick="toggleLock()">&#128275; Unlock</button></div></div>
<div id="p1" class="p"><div style="display:flex;gap:14px;flex-wrap:wrap;margin-bottom:14px"><div style="flex:1;min-width:280px;background:#fff;padding:20px;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,.15)"><h3 style="color:#667eea;margin:0 0 16px 0">&#128179; Add New User</h3><input id="suid" placeholder="Card UID (scan or type)" style="margin-bottom:10px;padding:12px;font-size:15px"><input id="sname" placeholder="Full Name" style="margin-bottom:10px;padding:12px;font-size:15px"><input id="sbal" placeholder="Balance (Rs)" type="number" value="0" style="margin-bottom:10px;padding:12px;font-size:15px"><input id="scl" placeholder="Credit Limit (Rs)" type="number" value="0" style="margin-bottom:16px;padding:12px;font-size:15px"><div style="background:linear-gradient(135deg,#56ab2f,#a8e063);padding:18px;border-radius:10px;text-align:center;cursor:pointer;box-shadow:0 4px 15px rgba(86,171,47,.4)" onclick="addScanned()"><span style="color:#fff;font-size:20px;font-weight:bold">&#10004; SUBMIT - ADD USER</span></div><div style="margin-top:14px;padding-top:14px;border-top:1px solid #eee;text-align:center"><button class="b bp" onclick="startScan()" style="padding:10px 24px">&#128179; Scan RFID Card</button><span id="scanStatus" style="margin-left:10px;padding:6px 12px;border-radius:6px;background:#f0f0f0;font-size:13px">Ready</span></div></div><div style="flex:1;min-width:280px;background:#fff;padding:20px;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,.15)"><h3 style="color:#0083b0;margin:0 0 16px 0">&#128176; Recharge User</h3><input id="ruid" placeholder="Existing User UID" style="margin-bottom:10px;padding:12px;font-size:15px"><input id="ramt" placeholder="Recharge Amount (Rs)" type="number" min="1" step="1" style="margin-bottom:16px;padding:12px;font-size:15px"><div style="background:linear-gradient(135deg,#00b4db,#0083b0);padding:18px;border-radius:10px;text-align:center;cursor:pointer;box-shadow:0 4px 15px rgba(0,131,176,.35)" onclick="rechargeUser()"><span style="color:#fff;font-size:20px;font-weight:bold">+ RECHARGE BALANCE</span></div><div style="margin-top:14px;padding-top:14px;border-top:1px solid #eee;text-align:center"><button class="b bb" onclick="startRechargeScan()" style="padding:10px 24px">&#128179; Scan User Card</button><span id="rechargeStatus" style="margin-left:10px;padding:6px 12px;border-radius:6px;background:#f0f0f0;font-size:13px">Ready</span></div></div></div><button class="b bg" onclick="sm('au')">+ Add Manually</button><button class="b bb" onclick="dlU('csv')">&#128196; CSV</button><button class="b bk" onclick="dlU('txt')">&#128196; TXT</button><table><thead><tr><th>UID</th><th>Name</th><th>Balance</th><th>Credit</th><th>Actions</th></tr></thead><tbody id="ut"></tbody></table></div>
<div id="p2" class="p"><button class="b bg" onclick="sm('am')">+ Add Item</button><button class="b bb" onclick="dlM('csv')">&#128196; CSV</button><button class="b bk" onclick="dlM('txt')">&#128196; TXT</button><table><thead><tr><th>Key</th><th>Name</th><th>Price</th><th>Stock</th><th>Actions</th></tr></thead><tbody id="mt"></tbody></table></div>
<div id="p3" class="p"><button class="b bp" onclick="lo()">&#8635; Refresh</button><button class="b bb" onclick="dlO('csv')">&#128196; CSV</button><button class="b bk" onclick="dlO('txt')">&#128196; TXT</button><div id="sal"></div><div id="ol"></div></div>
<div id="p4" class="p"><div class="stats"><div class="stat"><b id="ts">&#8377;0</b><span>Total Sales</span></div><div class="stat"><b id="tord">0</b><span>Total Orders</span></div><div class="stat"><b id="trch">&#8377;0</b><span>Recharges</span></div></div><div style="display:flex;gap:6px;flex-wrap:wrap;margin-bottom:12px"><button class="b bp" onclick="lrp()">&#8635; Refresh</button><button class="b bb" onclick="dlRp('sales')">&#128200; Sales</button><button class="b bg" onclick="dlRp('inv')">&#128230; Inventory</button><button class="b by" onclick="dlRp('user')">&#128100; Users</button></div><div style="display:flex;gap:12px;flex-wrap:wrap"><div style="flex:1;min-width:280px"><b>&#128200; Sales Analytics</b><div id="sc" style="margin-top:8px"></div></div><div style="flex:1;min-width:280px"><b>&#127942; Top Items</b><div id="ti" style="margin-top:8px"></div></div></div><div style="margin-top:14px"><b>&#128179; Transaction History</b><table><thead><tr><th>Time</th><th>Type</th><th>User</th><th>Amount</th><th>Order#</th></tr></thead><tbody id="txl"></tbody></table></div><div style="margin-top:14px"><b>&#128100; User Activity</b><div id="ua" style="margin-top:8px"></div></div></div>
</div></div>
<div id="au" class="m"><div class="mc"><b>&#128100; Add New User</b><br><input id="nuid" placeholder="&#128179; Card UID"><input id="nname" placeholder="&#128221; Full Name"><input id="nbal" placeholder="&#128176; Initial Balance" type="number" value="0"><input id="ncl" placeholder="&#128179; Credit Limit" type="number" value="0"><button class="b bg" onclick="addU()">&#10004; Add</button><button class="b br" onclick="hm('au')">&#10006; Cancel</button></div></div>
<div id="am" class="m"><div class="mc"><b>&#127828; Add Menu Item</b><br><input id="mkey" placeholder="&#128289; Key (e.g. B01)" maxlength="3"><input id="mname" placeholder="&#128221; Item Name"><input id="mprice" placeholder="&#128176; Price (Rs)" type="number"><input id="mstock" placeholder="&#128230; Stock Qty" type="number" value="50"><button class="b bg" onclick="addM()">&#10004; Add</button><button class="b br" onclick="hm('am')">&#10006; Cancel</button></div></div>
<div id="eu" class="m"><div class="mc"><b>&#9998; Edit User</b><br><input id="euid" readonly placeholder="UID"><input id="ename" placeholder="&#128221; Name"><input id="ebal" placeholder="&#128176; Balance" type="number"><input id="ecl" placeholder="&#128179; Credit Limit" type="number"><button class="b bg" onclick="updU()">&#10004; Update</button><button class="b br" onclick="hm('eu')">&#10006; Cancel</button></div></div>
<script>
var U=[],M=[],O=[],SA=[],TX=[],SR={},IR={},UR={},isLocked=false,loggedIn=false;
function $(i){return document.getElementById(i)}
function doLogin(){var u=$('auser').value,p=$('apass').value;fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({user:u,pass:p})}).then(r=>r.json()).then(d=>{if(d.success){loggedIn=true;localStorage.setItem('sct','1');$('loginScreen').style.display='none';$('mainApp').style.display='block';rf()}else{$('lerr').style.display='block';setTimeout(()=>$('lerr').style.display='none',3000)}})}
function doLogout(){loggedIn=false;localStorage.removeItem('sct');$('loginScreen').style.display='flex';$('mainApp').style.display='none';$('auser').value='';$('apass').value=''}
function checkSession(){if(localStorage.getItem('sct')==='1'){$('loginScreen').style.display='none';$('mainApp').style.display='block';loggedIn=true;rf()}$('ipaddr').textContent=location.host}
function sw(n){for(var i=0;i<5;i++){$('p'+i).className=i==n?'p a':'p';document.querySelectorAll('.t')[i].className=i==n?'t a':'t'}if(n==1)lu();if(n==2)lm();if(n==3)lo();if(n==4)lrp()}
function sm(i){$(i).className='m s'}
function hm(i){$(i).className='m'}
function rf(){lu();lm();lo();lsa();gls()}
function gls(){fetch('/api/system/status').then(r=>r.json()).then(d=>{isLocked=d.locked;$('lockBanner').style.display=d.locked?'block':'none';$('lockMsg').textContent=d.message||'System Closed';$('lockBtn').innerHTML=d.locked?'&#128275; Unlock System':'&#128274; Lock System';$('lockBtn').className=d.locked?'b bg':'b br'})}
function toggleLock(){if(isLocked){fetch('/api/system/unlock',{method:'POST'}).then(()=>gls())}else{var msg=prompt('Lock message (optional):','Canteen Closed');if(msg!==null)fetch('/api/system/lock',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({message:msg||'System Closed'})}).then(()=>gls())}}
function lu(){fetch('/api/users').then(r=>r.json()).then(d=>{U=d;var h='';d.forEach((u,i)=>{h+='<tr><td><code>'+u.uid+'</code></td><td><b>'+u.name+'</b></td><td>&#8377;'+u.balance.toFixed(0)+'</td><td>&#8377;'+u.creditLimit.toFixed(0)+'</td><td><button class="b by" onclick="edU('+i+')">&#9998;</button><button class="b br" onclick="delU('+i+')">&#128465;</button></td></tr>'});$('ut').innerHTML=h;$('tu').textContent=d.length;$('on').textContent=d.filter(x=>x.online).length})}
function sb(s){if(s==0)return'<span style="background:#6c757d;color:#fff;padding:2px 8px;border-radius:10px;font-size:11px">&#10060; OUT</span>';if(s<10)return'<span style="background:linear-gradient(135deg,#eb3349,#f45c43);color:#fff;padding:2px 8px;border-radius:10px;font-size:11px">&#128308; '+s+'</span>';if(s<25)return'<span style="background:linear-gradient(135deg,#f7971e,#ffd200);color:#333;padding:2px 8px;border-radius:10px;font-size:11px">&#128993; '+s+'</span>';return'<span style="background:linear-gradient(135deg,#56ab2f,#a8e063);color:#fff;padding:2px 8px;border-radius:10px;font-size:11px">&#128994; '+s+'</span>'}
function lm(){fetch('/api/menu').then(r=>r.json()).then(d=>{M=d;var h='';d.forEach((m,i)=>{h+='<tr><td><code>'+m.key+'</code></td><td><b>'+m.name+'</b></td><td>&#8377;'+m.price.toFixed(0)+'</td><td>'+sb(m.stock)+'</td><td><button class="b by" onclick="edM('+i+')">&#9998;</button><button class="b br" onclick="delM('+i+')">&#128465;</button></td></tr>'});$('mt').innerHTML=h;$('tm').textContent=d.length})}
function lo(){fetch('/api/orders').then(r=>r.json()).then(d=>{O=d;var h='',p=0;d.forEach(o=>{if(o.status=='pending')p++;var cls=o.status=='ready'?'o r':o.status=='cancelled'?'o c':'o';h+='<div class="'+cls+'"><b>&#127915; #'+o.id+'</b> - <b>'+o.userName+'</b><span class="st s'+o.status[0]+'">'+o.status.toUpperCase()+'</span><br><span style="font-size:11px;color:#666">&#128337; '+(o.time||'--')+'</span><br>';o.items.forEach(it=>{h+='&#8226; '+it.name+' &times;'+it.qty+'<br>'});h+='<b>&#8377; '+o.total.toFixed(0)+'</b>';if(o.status=='pending')h+='<div style="display:flex;gap:8px;margin-top:12px"><div style="flex:1;background:linear-gradient(135deg,#56ab2f,#a8e063);padding:14px;border-radius:8px;text-align:center;cursor:pointer;color:#fff;font-weight:bold;font-size:16px" onclick="rdy('+o.id+')">&#10004; READY</div><div style="flex:1;background:linear-gradient(135deg,#eb3349,#f45c43);padding:14px;border-radius:8px;text-align:center;cursor:pointer;color:#fff;font-weight:bold;font-size:16px" onclick="cnl('+o.id+')">&#10006; CANCEL</div></div>';h+='</div>'});$('ol').innerHTML=h||'<div style="text-align:center;padding:30px;color:#888">&#127860; No orders yet</div>';$('to').textContent=p});lsa()}
function lsa(){fetch('/api/stock-alerts').then(r=>r.json()).then(d=>{SA=d;var h='',c=0;d.forEach((a,i)=>{if(!a.ack){c++;h+='<div style="background:linear-gradient(135deg,#fff5f5,#ffe0e0);border-left:4px solid #eb3349;padding:12px;margin:8px 0;border-radius:8px;display:flex;justify-content:space-between;align-items:center"><div><b style="color:#eb3349">&#9888; LOW STOCK</b><br><b>['+a.key+']</b> '+a.name+' - Only <b style="color:#eb3349">'+a.stock+'</b> left!</div><button class="b by" onclick="ackSA(\''+a.key+'\')" style="white-space:nowrap">&#10004; Acknowledge</button></div>'}});$('sal').innerHTML=h;$('sa').textContent=c;$('sas').style.background=c>0?'linear-gradient(135deg,#ffe0e0,#ffcccc)':'';if(c>0&&'speechSynthesis'in window){var u=new SpeechSynthesisUtterance('Warning: '+c+' items running low on stock');speechSynthesis.speak(u)}})}
function ackSA(k){fetch('/api/stock-alerts/'+k+'/ack',{method:'POST'}).then(()=>lsa())}
function reqJson(u,o){return fetch(u,o).then(r=>r.text().then(t=>{var d={};try{d=t?JSON.parse(t):{}}catch(e){}if(!r.ok||(d&&d.error))throw new Error((d&&d.error)||'Request failed');return d}))}
function userExists(uid){uid=(uid||'').trim();return U.some(u=>u.uid===uid)}
function setStatus(id,text,color){$(id).textContent=text;$(id).style.background=color}
function clearAddForm(){$('suid').value='';$('sname').value='';$('sbal').value='0';$('scl').value='0'}
function clearManualAddForm(){$('nuid').value='';$('nname').value='';$('nbal').value='0';$('ncl').value='0'}
function startScanFor(type,inputId,statusId,focusId){setStatus(statusId,'Scanning...','#fff3cd');reqJson('/api/scan-mode?type='+type,{method:'POST'}).then(()=>{if(scanInterval)clearInterval(scanInterval);scanInterval=setInterval(()=>{reqJson('/api/last-scanned').then(d=>{if(d.uid&&d.uid.length>0){clearInterval(scanInterval);$(inputId).value=d.uid;if(type=='add'&&userExists(d.uid)){setStatus(statusId,'UID already exists','#f8d7da')}else{setStatus(statusId,'Card found!','#d4edda');if(focusId)$(focusId).focus()}reqJson('/api/clear-scanned',{method:'POST'})}}).catch(()=>{})},1000)}).catch(e=>setStatus(statusId,e.message,'#f8d7da'))}
function addU(){var d={uid:$('nuid').value.trim(),name:$('nname').value.trim(),balance:+$('nbal').value,creditLimit:+$('ncl').value};if(!d.uid||!d.name){alert('Enter UID and Name');return}if(userExists(d.uid)){alert('User with this UID already exists');return}reqJson('/api/users',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).then(()=>{clearManualAddForm();hm('au');lu()}).catch(e=>alert(e.message))}
function addM(){var d={key:$('mkey').value.toUpperCase(),name:$('mname').value,price:+$('mprice').value,stock:+$('mstock').value};fetch('/api/menu',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).then(()=>{hm('am');lm()})}
function edU(i){var u=U[i];$('euid').value=u.uid;$('ename').value=u.name;$('ebal').value=u.balance;$('ecl').value=u.creditLimit;sm('eu')}
function updU(){var d={name:$('ename').value,balance:+$('ebal').value,creditLimit:+$('ecl').value};fetch('/api/users/'+$('euid').value,{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).then(()=>{hm('eu');lu()})}
function delU(i){if(confirm('Delete '+U[i].name+'?'))fetch('/api/users/'+U[i].uid,{method:'DELETE'}).then(()=>lu())}
function edM(i){var s=prompt('New stock for '+M[i].name+':',M[i].stock);if(s!=null){var p=prompt('New price:',M[i].price);if(p!=null)fetch('/api/menu/'+M[i].key,{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({stock:+s,price:+p})}).then(()=>lm())}}
function delM(i){if(confirm('Delete '+M[i].name+'?'))fetch('/api/menu/'+M[i].key,{method:'DELETE'}).then(()=>lm())}
function rdy(id){var ord=O.find(o=>o.id==id);fetch('/api/orders/'+id+'/ready',{method:'POST'}).then(()=>{lo();if('speechSynthesis'in window){var u=new SpeechSynthesisUtterance('Order ready for '+(ord?ord.userName:'customer'));speechSynthesis.speak(u)}})}
function cnl(id){if(confirm('Cancel order?'))fetch('/api/orders/'+id+'/cancel',{method:'POST'}).then(()=>lo())}
var scanInterval=null;
function startScan(){startScanFor('add','suid','scanStatus','sname')}
function startRechargeScan(){startScanFor('recharge','ruid','rechargeStatus','ramt')}
function addScanned(){var d={uid:$('suid').value.trim(),name:$('sname').value.trim(),balance:+$('sbal').value,creditLimit:+$('scl').value};if(!d.uid||!d.name){alert('Enter UID and Name');return}if(userExists(d.uid)){setStatus('scanStatus','UID already exists','#f8d7da');alert('User with this UID already exists');return}reqJson('/api/users',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).then(()=>{clearAddForm();lu();setStatus('scanStatus','Added!','#d4edda');setTimeout(()=>setStatus('scanStatus','Ready','#f0f0f0'),2000)}).catch(e=>{setStatus('scanStatus',e.message,'#f8d7da');alert(e.message)})}
function rechargeUser(){var uid=$('ruid').value.trim(),amount=+$('ramt').value;if(!uid){alert('Enter UID');return}if(!(amount>0)){alert('Enter valid recharge amount');return}reqJson('/api/users/'+encodeURIComponent(uid)+'/recharge',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({amount:amount})}).then(d=>{var balance=typeof d.balance==='number'?d.balance.toFixed(0):amount.toFixed(0);$('ramt').value='';setStatus('rechargeStatus','Balance updated: Rs '+balance,'#d4edda');lu();ltx();lsr()}).catch(e=>{setStatus('rechargeStatus',e.message,'#f8d7da');alert(e.message)})}
function sc(){startScan()}
function dl(n,t,c){var a=document.createElement('a');a.href='data:text/'+c+';charset=utf-8,'+encodeURIComponent(t);a.download=n;a.click()}
function dlU(f){if(!U.length)return alert('No users');var t='';if(f=='csv'){t='UID,Name,Balance,CreditUsed,CreditLimit\n';U.forEach(u=>t+=u.uid+','+u.name+','+u.balance+','+u.creditUsed+','+u.creditLimit+'\n');dl('users.csv',t,'csv')}else{t='SMART CANTEEN - USERS\n'+new Date().toLocaleString()+'\n\n';U.forEach(u=>t+='UID: '+u.uid+'\nName: '+u.name+'\nBalance: Rs '+u.balance+'\nCredit Used: Rs '+u.creditUsed+'\nCredit Limit: Rs '+u.creditLimit+'\n---\n');dl('users.txt',t,'plain')}}
function dlM(f){if(!M.length)return alert('No menu');var t='';if(f=='csv'){t='Key,Name,Price,Stock\n';M.forEach(m=>t+=m.key+','+m.name+','+m.price+','+m.stock+'\n');dl('menu.csv',t,'csv')}else{t='SMART CANTEEN - MENU\n'+new Date().toLocaleString()+'\n\n';M.forEach(m=>t+='['+m.key+'] '+m.name+'\nPrice: Rs '+m.price+' | Stock: '+m.stock+'\n---\n');dl('menu.txt',t,'plain')}}
function dlO(f){if(!O.length)return alert('No orders');var t='';if(f=='csv'){t='OrderID,Time,User,Status,Total,Items\n';O.forEach(o=>{var items=o.items.map(i=>i.name+'x'+i.qty).join(';');t+=o.id+','+(o.time||'')+','+o.userName+','+o.status+','+o.total+',\"'+items+'\"\n'});dl('orders.csv',t,'csv')}else{t='SMART CANTEEN - ORDERS\n'+new Date().toLocaleString()+'\n\n';O.forEach(o=>{t+='Order #'+o.id+' ['+(o.time||'--')+'] ['+o.status.toUpperCase()+']\nUser: '+o.userName+'\n';o.items.forEach(i=>t+='  - '+i.name+' x'+i.qty+'\n');t+='Total: Rs '+o.total+'\n---\n'});dl('orders.txt',t,'plain')}}
function lrp(){ltx();lsr();lir();lur()}
function ltx(){fetch('/api/transactions').then(r=>r.json()).then(d=>{TX=d;var h='';d.slice(0,20).forEach(t=>{var c=t.type=='order'?'#eb3349':t.type=='recharge'?'#56ab2f':'#f7971e';h+='<tr><td style="font-size:11px;color:#666">'+(t.time||'--')+'</td><td><span style="background:'+c+';color:#fff;padding:2px 6px;border-radius:4px;font-size:11px">'+t.type.toUpperCase()+'</span></td><td>'+t.userName+'</td><td>&#8377;'+t.amount.toFixed(0)+'</td><td>'+(t.orderId||'-')+'</td></tr>'});$('txl').innerHTML=h||'<tr><td colspan="5" style="text-align:center;color:#888">No transactions</td></tr>'})}
function lsr(){fetch('/api/reports/sales').then(r=>r.json()).then(d=>{SR=d;$('ts').innerHTML='&#8377;'+d.totalSales.toFixed(0);$('tord').textContent=d.totalOrders;$('trch').innerHTML='&#8377;'+d.totalRecharges.toFixed(0);var h='<div style="display:flex;gap:4px;align-items:flex-end;height:80px">';var mx=Math.max(...d.daily.map(x=>x.sales))||1;d.daily.forEach((x,i)=>{var ht=Math.max(10,x.sales/mx*70);h+='<div style="flex:1;text-align:center"><div style="background:linear-gradient(180deg,#667eea,#764ba2);height:'+ht+'px;border-radius:4px 4px 0 0"></div><div style="font-size:10px;color:#666">D'+(i+1)+'</div></div>'});h+='</div>';$('sc').innerHTML=h;var th='';d.topItems.slice(0,5).forEach((x,i)=>{th+='<div style="display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #eee"><span>'+(i+1)+'. '+x.name+'</span><b>'+x.qty+' sold</b></div>'});$('ti').innerHTML=th||'<div style="color:#888">No sales data</div>'})}
function lir(){fetch('/api/reports/inventory').then(r=>r.json()).then(d=>{IR=d})}
function lur(){fetch('/api/reports/users').then(r=>r.json()).then(d=>{UR=d;var h='';d.topUsers.slice(0,5).forEach((u,i)=>{h+='<div style="display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #eee"><span>'+(i+1)+'. '+u.name+'</span><b>&#8377;'+u.spent.toFixed(0)+' spent</b></div>'});$('ua').innerHTML=h||'<div style="color:#888">No activity data</div>'})}
function dlRp(t){if(t=='sales'){var txt='SALES REPORT\n'+new Date().toLocaleString()+'\n\nTotal Sales: Rs '+SR.totalSales+'\nTotal Orders: '+SR.totalOrders+'\nTotal Recharges: Rs '+SR.totalRecharges+'\n\nDaily Sales:\n';SR.daily.forEach((d,i)=>txt+='Day '+(i+1)+': Rs '+d.sales+' ('+d.orders+' orders)\n');txt+='\nTop Items:\n';SR.topItems.forEach((x,i)=>txt+=(i+1)+'. '+x.name+' - '+x.qty+' sold (Rs '+x.revenue+')\n');dl('sales_report.txt',txt,'plain')}else if(t=='inv'){var txt='INVENTORY REPORT\n'+new Date().toLocaleString()+'\n\n';IR.items.forEach(x=>txt+='['+x.key+'] '+x.name+'\nStock: '+x.stock+' | Sold: '+x.sold+' | Revenue: Rs '+x.revenue+'\n---\n');dl('inventory_report.txt',txt,'plain')}else{var txt='USER ACTIVITY REPORT\n'+new Date().toLocaleString()+'\n\nTop Spenders:\n';UR.topUsers.forEach((u,i)=>txt+=(i+1)+'. '+u.name+' - Rs '+u.spent+' ('+u.orders+' orders)\n');dl('user_activity.txt',txt,'plain')}}
function uclk(){var d=new Date();var days=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];var months=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];$('cdate').textContent=days[d.getDay()]+', '+d.getDate()+' '+months[d.getMonth()]+' '+d.getFullYear();$('ctime').textContent=d.toLocaleTimeString()}uclk();checkSession();setInterval(uclk,1000);setInterval(function(){if(loggedIn)rf()},15000);$('apass').addEventListener('keypress',function(e){if(e.key==='Enter')doLogin()});
</script></body></html>
)rawliteral";

// Function Declarations
void setupWiFi();
void setupRoutes();
void handleRoot();
void handleGetUsers();
void handleAddUser();
void handleUpdateUser();
void handleDeleteUser();
void handleRechargeUser();
void handleGetMenu();
void handleAddMenuItem();
void handleUpdateMenuItem();
void handleDeleteMenuItem();
void handleGetOrders();
void handleOrderReady();
void handleOrderCancel();
void handleScanMode();
void handleLastScanned();
void handleClearScanned();
void processSerialData();
void saveData();
void loadData();
int findUserByUID(const char* uid);
int findMenuItemByKey(const char* key);
void handleGetStockAlerts();
void handleAckStockAlert();
void checkAndCreateStockAlert(int menuIdx);
void handleSystemStatus();
void handleSystemLock();
void handleSystemUnlock();
void handleLogin();
void handleGetTransactions();
void handleGetSalesReport();
void handleGetInventoryReport();
void handleGetUserActivityReport();
void addTransaction(int orderId, const char* uid, const char* name, float amount, const char* type);
void getCurrentTimeStr(char* buffer);
void sendBalanceUpdate(const User& user);

// Last scanned UID for web interface
char lastScannedUID[20] = "";
enum WebScanMode { SCAN_NONE, SCAN_ADD_USER, SCAN_RECHARGE_USER };
WebScanMode webScanMode = SCAN_NONE;

void setup() {
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);
  
  // Load saved data
  loadData();
  
  // Setup WiFi
  setupWiFi();
  
  // Setup HTTP routes
  setupRoutes();
  
  // Start server
  server.begin();
}

void loop() {
  server.handleClient();
  processSerialData();
  
  // Check online user timeouts
  unsigned long now = millis();
  for (int i = 0; i < onlineCount; i++) {
    if (now - onlineTimestamps[i] > 15000) {
      // User timed out
      for (int j = i; j < onlineCount - 1; j++) {
        strcpy(onlineUsers[j], onlineUsers[j + 1]);
        onlineTimestamps[j] = onlineTimestamps[j + 1];
      }
      onlineCount--;
      i--;
    }
  }
}

void setupWiFi() {
  // Set WiFi mode and disconnect any previous connection
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  // Configure static IP if enabled
  #if USE_STATIC_IP
  if (!WiFi.config(staticIP, gateway, subnet, dns, dns)) {
    Serial.println("Static IP Failed");
  }
  #endif
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    // Print IP to serial (Arduino can display it)
    Serial.print("IP:");
    Serial.println(WiFi.localIP().toString());
    
    // Initialize NTP for real time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  } else {
    Serial.println("WiFi Failed");
  }
}

// Get current time as string "DD/MM HH:MM"
void getCurrentTimeStr(char* buffer) {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  
  if (timeinfo->tm_year > 100) {  // Valid time (year > 2000)
    sprintf(buffer, "%02d/%02d %02d:%02d", 
            timeinfo->tm_mday, timeinfo->tm_mon + 1,
            timeinfo->tm_hour, timeinfo->tm_min);
  } else {
    strcpy(buffer, "-- --:--");
  }
}

void setupRoutes() {
  // Serve main page
  server.on("/", HTTP_GET, handleRoot);
  
  // User API
  server.on("/api/users", HTTP_GET, handleGetUsers);
  server.on("/api/users", HTTP_POST, handleAddUser);
  
  // Menu API
  server.on("/api/menu", HTTP_GET, handleGetMenu);
  server.on("/api/menu", HTTP_POST, handleAddMenuItem);
  
  // Orders API
  server.on("/api/orders", HTTP_GET, handleGetOrders);
  
  // Stock Alerts API
  server.on("/api/stock-alerts", HTTP_GET, handleGetStockAlerts);
  
  // Scan mode
  server.on("/api/scan-mode", HTTP_POST, handleScanMode);
  server.on("/api/last-scanned", HTTP_GET, handleLastScanned);
  server.on("/api/clear-scanned", HTTP_POST, handleClearScanned);
  
  // System Lock API
  server.on("/api/system/status", HTTP_GET, handleSystemStatus);
  server.on("/api/system/lock", HTTP_POST, handleSystemLock);
  server.on("/api/system/unlock", HTTP_POST, handleSystemUnlock);
  
  // Login API
  server.on("/api/login", HTTP_POST, handleLogin);
  
  // Reports API
  server.on("/api/transactions", HTTP_GET, handleGetTransactions);
  server.on("/api/reports/sales", HTTP_GET, handleGetSalesReport);
  server.on("/api/reports/inventory", HTTP_GET, handleGetInventoryReport);
  server.on("/api/reports/users", HTTP_GET, handleGetUserActivityReport);
  
  // Dynamic routes for specific resources
  server.onNotFound([]() {
    String uri = server.uri();
    String method = (server.method() == HTTP_PUT) ? "PUT" : 
                    (server.method() == HTTP_DELETE) ? "DELETE" : 
                    (server.method() == HTTP_POST) ? "POST" : "GET";
    
    // User routes: /api/users/{uid}
    if (uri.startsWith("/api/users/") && !uri.endsWith("/recharge")) {
      String uid = uri.substring(11);
      if (method == "PUT") {
        handleUpdateUser();
      } else if (method == "DELETE") {
        handleDeleteUser();
      }
      return;
    }
    
    // User recharge: /api/users/{uid}/recharge
    if (uri.startsWith("/api/users/") && uri.endsWith("/recharge")) {
      handleRechargeUser();
      return;
    }
    
    // Menu routes: /api/menu/{key}
    if (uri.startsWith("/api/menu/")) {
      String key = uri.substring(10);
      if (method == "PUT") {
        handleUpdateMenuItem();
      } else if (method == "DELETE") {
        handleDeleteMenuItem();
      }
      return;
    }
    
    // Order routes: /api/orders/{id}/ready or /api/orders/{id}/cancel
    if (uri.startsWith("/api/orders/") && method == "POST") {
      if (uri.endsWith("/ready")) {
        handleOrderReady();
      } else if (uri.endsWith("/cancel")) {
        handleOrderCancel();
      }
      return;
    }
    
    // Stock alert acknowledge: /api/stock-alerts/{key}/ack
    if (uri.startsWith("/api/stock-alerts/") && uri.endsWith("/ack") && method == "POST") {
      handleAckStockAlert();
      return;
    }
    
    server.send(404, "text/plain", "Not Found");
  });
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleGetUsers() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  
  for (int i = 0; i < userCount; i++) {
    if (users[i].active) {
      JsonObject obj = arr.createNestedObject();
      obj["uid"] = users[i].uid;
      obj["name"] = users[i].name;
      obj["balance"] = users[i].balance;
      obj["creditUsed"] = users[i].creditUsed;
      obj["creditLimit"] = users[i].creditLimit;
      
      // Check if online
      bool online = false;
      for (int j = 0; j < onlineCount; j++) {
        if (strcmp(onlineUsers[j], users[i].uid) == 0) {
          online = true;
          break;
        }
      }
      obj["online"] = online;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAddUser() {
  if (userCount >= MAX_USERS) {
    server.send(400, "application/json", "{\"error\":\"Max users reached\"}");
    return;
  }
  
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }
  
  String uid = doc["uid"] | "";
  String name = doc["name"] | "";
  uid.trim();
  name.trim();
  
  if (!uid.length() || !name.length()) {
    server.send(400, "application/json", "{\"error\":\"UID and name are required\"}");
    return;
  }
  
  for (int i = 0; i < userCount; i++) {
    if (strcmp(users[i].uid, uid.c_str()) == 0) {
      server.send(409, "application/json", "{\"error\":\"User with this UID already exists\"}");
      return;
    }
  }
  
  float balance = doc["balance"] | 0.0;
  float creditLimit = doc["creditLimit"] | 0.0;
  if (balance < 0) balance = 0;
  if (creditLimit < 0) creditLimit = 0;
  
  strncpy(users[userCount].uid, uid.c_str(), 19);
  users[userCount].uid[19] = '\0';
  strncpy(users[userCount].name, name.c_str(), 19);
  users[userCount].name[19] = '\0';
  users[userCount].balance = balance;
  users[userCount].creditUsed = 0;
  users[userCount].creditLimit = creditLimit;
  users[userCount].active = true;
  userCount++;
  
  saveData();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleUpdateUser() {
  String uri = server.uri();
  String uid = uri.substring(11);
  
  int idx = findUserByUID(uid.c_str());
  if (idx < 0) {
    server.send(404, "application/json", "{\"error\":\"User not found\"}");
    return;
  }
  
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }
  
  bool balanceChanged = false;
  
  if (doc.containsKey("name")) {
    String name = doc["name"] | "";
    name.trim();
    if (!name.length()) {
      server.send(400, "application/json", "{\"error\":\"Name is required\"}");
      return;
    }
    strncpy(users[idx].name, name.c_str(), 19);
    users[idx].name[19] = '\0';
  }
  if (doc.containsKey("balance")) {
    users[idx].balance = doc["balance"];
    balanceChanged = true;
  }
  if (doc.containsKey("creditUsed")) {
    users[idx].creditUsed = doc["creditUsed"];
    balanceChanged = true;
  }
  if (doc.containsKey("creditLimit")) {
    users[idx].creditLimit = doc["creditLimit"];
    balanceChanged = true;
  }
  
  if (balanceChanged) sendBalanceUpdate(users[idx]);
  saveData();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleDeleteUser() {
  String uri = server.uri();
  String uid = uri.substring(11);
  
  int idx = findUserByUID(uid.c_str());
  if (idx >= 0) {
    users[idx].active = false;
    saveData();
  }
  server.send(200, "application/json", "{\"success\":true}");
}

void handleRechargeUser() {
  String uri = server.uri();
  int endIdx = uri.indexOf("/recharge");
  String uid = uri.substring(11, endIdx);
  
  int idx = findUserByUID(uid.c_str());
  if (idx < 0) {
    server.send(404, "application/json", "{\"error\":\"User not found\"}");
    return;
  }
  
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }
  
  float amount = doc["amount"] | 0.0;
  if (amount <= 0) {
    server.send(400, "application/json", "{\"error\":\"Recharge amount must be greater than zero\"}");
    return;
  }
  users[idx].balance += amount;
  sendBalanceUpdate(users[idx]);
  
  // Record recharge transaction
  addTransaction(0, users[idx].uid, users[idx].name, amount, "recharge");
  
  saveData();
  DynamicJsonDocument responseDoc(256);
  responseDoc["success"] = true;
  responseDoc["uid"] = users[idx].uid;
  responseDoc["balance"] = users[idx].balance;
  responseDoc["creditUsed"] = users[idx].creditUsed;
  responseDoc["creditLimit"] = users[idx].creditLimit;
  String response;
  serializeJson(responseDoc, response);
  server.send(200, "application/json", response);
}

void handleGetMenu() {
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  
  for (int i = 0; i < menuItemCount; i++) {
    if (menuItems[i].active) {
      JsonObject obj = arr.createNestedObject();
      obj["key"] = menuItems[i].key;
      obj["name"] = menuItems[i].name;
      obj["price"] = menuItems[i].price;
      obj["stock"] = menuItems[i].stock;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAddMenuItem() {
  if (menuItemCount >= MAX_MENU_ITEMS) {
    server.send(400, "application/json", "{\"error\":\"Max items reached\"}");
    return;
  }
  
  DynamicJsonDocument doc(256);
  deserializeJson(doc, server.arg("plain"));
  
  strncpy(menuItems[menuItemCount].key, doc["key"] | "", 3);
  strncpy(menuItems[menuItemCount].name, doc["name"] | "", 19);
  menuItems[menuItemCount].price = doc["price"] | 0.0;
  menuItems[menuItemCount].stock = doc["stock"] | 0;
  menuItems[menuItemCount].active = true;
  menuItemCount++;
  
  saveData();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleUpdateMenuItem() {
  String uri = server.uri();
  String key = uri.substring(10);
  
  int idx = findMenuItemByKey(key.c_str());
  if (idx < 0) {
    server.send(404, "application/json", "{\"error\":\"Item not found\"}");
    return;
  }
  
  DynamicJsonDocument doc(256);
  deserializeJson(doc, server.arg("plain"));
  
  if (doc.containsKey("price")) menuItems[idx].price = doc["price"];
  if (doc.containsKey("stock")) menuItems[idx].stock = doc["stock"];
  if (doc.containsKey("name")) strncpy(menuItems[idx].name, doc["name"], 19);
  
  saveData();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleDeleteMenuItem() {
  String uri = server.uri();
  String key = uri.substring(10);
  
  int idx = findMenuItemByKey(key.c_str());
  if (idx >= 0) {
    menuItems[idx].active = false;
    saveData();
  }
  server.send(200, "application/json", "{\"success\":true}");
}

void handleGetOrders() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  
  for (int i = 0; i < orderCount; i++) {
    if (orders[i].active) {
      JsonObject obj = arr.createNestedObject();
      obj["id"] = orders[i].id;
      obj["uid"] = orders[i].uid;
      obj["userName"] = orders[i].userName;
      obj["total"] = orders[i].total;
      obj["status"] = orders[i].status;
      obj["time"] = orders[i].timeStr;
      
      JsonArray items = obj.createNestedArray("items");
      for (int j = 0; j < orders[i].itemCount; j++) {
        JsonObject item = items.createNestedObject();
        item["key"] = orders[i].items[j].key;
        item["qty"] = orders[i].items[j].qty;
        
        // Find item name
        int menuIdx = findMenuItemByKey(orders[i].items[j].key);
        if (menuIdx >= 0) {
          item["name"] = menuItems[menuIdx].name;
        } else {
          item["name"] = orders[i].items[j].key;
        }
      }
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleOrderReady() {
  String uri = server.uri();
  int startIdx = 12;  // After "/api/orders/"
  int endIdx = uri.indexOf("/ready");
  String idStr = uri.substring(startIdx, endIdx);
  int orderId = idStr.toInt();
  
  for (int i = 0; i < orderCount; i++) {
    if (orders[i].id == orderId) {
      strcpy(orders[i].status, "ready");
      
      // Send notification to Arduino
      Serial.print("ORDER_STATUS,");
      Serial.print(orders[i].uid);
      Serial.println(",READY");
      
      break;
    }
  }
  
  server.send(200, "application/json", "{\"success\":true}");
}

void handleOrderCancel() {
  String uri = server.uri();
  int startIdx = 12;
  int endIdx = uri.indexOf("/cancel");
  String idStr = uri.substring(startIdx, endIdx);
  int orderId = idStr.toInt();
  
  for (int i = 0; i < orderCount; i++) {
    if (orders[i].id == orderId) {
      strcpy(orders[i].status, "cancelled");
      
      // Refund the user
      int userIdx = findUserByUID(orders[i].uid);
      if (userIdx >= 0) {
        users[userIdx].balance += orders[i].total;
        
        // Record refund transaction
        addTransaction(orders[i].id, orders[i].uid, orders[i].userName, orders[i].total, "refund");
        
        // Restore stock and reduce item sales
        for (int j = 0; j < orders[i].itemCount; j++) {
          int menuIdx = findMenuItemByKey(orders[i].items[j].key);
          if (menuIdx >= 0) {
            menuItems[menuIdx].stock += orders[i].items[j].qty;
            if (itemSales[menuIdx] >= orders[i].items[j].qty) {
              itemSales[menuIdx] -= orders[i].items[j].qty;
            }
          }
        }
        
        // Adjust daily sales
        if (dailySales[0] >= orders[i].total) {
          dailySales[0] -= orders[i].total;
        }
        if (dailyOrders[0] > 0) {
          dailyOrders[0]--;
        }
      }
      
      if (userIdx >= 0) sendBalanceUpdate(users[userIdx]);

      Serial.print("ORDER_STATUS,");
      Serial.print(orders[i].uid);
      Serial.println(",CANCELLED");
      
      saveData();
      break;
    }
  }
  
  server.send(200, "application/json", "{\"success\":true}");
}

void handleScanMode() {
  lastScannedUID[0] = '\0';
  String scanType = server.arg("type");
  webScanMode = (scanType == "recharge") ? SCAN_RECHARGE_USER : SCAN_ADD_USER;
  Serial.println("RECHARGE_MODE");
  server.send(200, "application/json", "{\"success\":true}");
}

void handleLastScanned() {
  String response = "{\"uid\":\"";
  response += lastScannedUID;
  response += "\"}";
  server.send(200, "application/json", response);
}

void handleClearScanned() {
  lastScannedUID[0] = '\0';
  webScanMode = SCAN_NONE;
  server.send(200, "application/json", "{\"success\":true}");
}

void handleSystemStatus() {
  String response = "{\"locked\":";
  response += systemLocked ? "true" : "false";
  response += ",\"message\":\"";
  response += lockMessage;
  response += "\"}";
  server.send(200, "application/json", response);
}

void handleSystemLock() {
  DynamicJsonDocument doc(256);
  deserializeJson(doc, server.arg("plain"));
  
  systemLocked = true;
  const char* msg = doc["message"] | "System Closed";
  strncpy(lockMessage, msg, 31);
  lockMessage[31] = '\0';
  
  // Notify Arduino about lock
  Serial.println("SYSTEM_LOCKED");
  
  server.send(200, "application/json", "{\"success\":true}");
}

void handleSystemUnlock() {
  systemLocked = false;
  strcpy(lockMessage, "System Closed");
  
  // Notify Arduino about unlock
  Serial.println("SYSTEM_UNLOCKED");
  
  server.send(200, "application/json", "{\"success\":true}");
}

void handleLogin() {
  DynamicJsonDocument doc(256);
  deserializeJson(doc, server.arg("plain"));
  
  const char* user = doc["user"] | "";
  const char* pass = doc["pass"] | "";
  
  if (strcmp(user, adminUser) == 0 && strcmp(pass, adminPass) == 0) {
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(200, "application/json", "{\"success\":false}");
  }
}

void handleGetStockAlerts() {
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.to<JsonArray>();
  
  for (int i = 0; i < stockAlertCount; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["key"] = stockAlerts[i].key;
    obj["name"] = stockAlerts[i].name;
    obj["stock"] = stockAlerts[i].stock;
    obj["ack"] = stockAlerts[i].acknowledged;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAckStockAlert() {
  String uri = server.uri();
  int startIdx = 18;  // After "/api/stock-alerts/"
  int endIdx = uri.indexOf("/ack");
  String key = uri.substring(startIdx, endIdx);
  
  for (int i = 0; i < stockAlertCount; i++) {
    if (strcmp(stockAlerts[i].key, key.c_str()) == 0) {
      stockAlerts[i].acknowledged = true;
      break;
    }
  }
  
  server.send(200, "application/json", "{\"success\":true}");
}

void checkAndCreateStockAlert(int menuIdx) {
  if (menuIdx < 0 || menuIdx >= menuItemCount) return;
  if (menuItems[menuIdx].stock >= LOW_STOCK_THRESHOLD) return;
  
  // Check if alert already exists for this item
  for (int i = 0; i < stockAlertCount; i++) {
    if (strcmp(stockAlerts[i].key, menuItems[menuIdx].key) == 0) {
      // Update existing alert with new stock level
      stockAlerts[i].stock = menuItems[menuIdx].stock;
      stockAlerts[i].acknowledged = false;  // Re-alert if stock dropped further
      return;
    }
  }
  
  // Create new alert
  if (stockAlertCount < MAX_STOCK_ALERTS) {
    strncpy(stockAlerts[stockAlertCount].key, menuItems[menuIdx].key, 3);
    stockAlerts[stockAlertCount].key[3] = '\0';
    strncpy(stockAlerts[stockAlertCount].name, menuItems[menuIdx].name, 19);
    stockAlerts[stockAlertCount].name[19] = '\0';
    stockAlerts[stockAlertCount].stock = menuItems[menuIdx].stock;
    stockAlerts[stockAlertCount].timestamp = millis();
    stockAlerts[stockAlertCount].acknowledged = false;
    stockAlertCount++;
  }
}

void addTransaction(int orderId, const char* uid, const char* name, float amount, const char* type) {
  if (transactionCount >= MAX_TRANSACTIONS) {
    // Shift transactions to make room
    for (int i = 0; i < MAX_TRANSACTIONS - 1; i++) {
      transactions[i] = transactions[i + 1];
    }
    transactionCount = MAX_TRANSACTIONS - 1;
  }
  
  Transaction& tx = transactions[transactionCount];
  tx.orderId = orderId;
  strncpy(tx.uid, uid, 19);
  tx.uid[19] = '\0';
  strncpy(tx.userName, name, 19);
  tx.userName[19] = '\0';
  tx.amount = amount;
  strncpy(tx.type, type, 9);
  tx.type[9] = '\0';
  getCurrentTimeStr(tx.timeStr);
  tx.timestamp = millis();
  tx.active = true;
  transactionCount++;
}

void sendBalanceUpdate(const User& user) {
  Serial.print("BAL_UPDATE,");
  Serial.print(user.uid);
  Serial.print(",");
  Serial.print(user.balance, 2);
  Serial.print(",");
  Serial.print(user.creditUsed, 2);
  Serial.print(",");
  Serial.println(user.creditLimit, 2);
}

void handleGetTransactions() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  
  // Return transactions in reverse order (newest first)
  for (int i = transactionCount - 1; i >= 0; i--) {
    if (transactions[i].active) {
      JsonObject obj = arr.createNestedObject();
      obj["orderId"] = transactions[i].orderId;
      obj["uid"] = transactions[i].uid;
      obj["userName"] = transactions[i].userName;
      obj["amount"] = transactions[i].amount;
      obj["type"] = transactions[i].type;
      obj["time"] = transactions[i].timeStr;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetSalesReport() {
  DynamicJsonDocument doc(2048);
  
  float totalSales = 0;
  int totalOrders = 0;
  float totalRecharges = 0;
  
  // Calculate totals from transactions
  for (int i = 0; i < transactionCount; i++) {
    if (!transactions[i].active) continue;
    if (strcmp(transactions[i].type, "order") == 0) {
      totalSales += transactions[i].amount;
      totalOrders++;
    } else if (strcmp(transactions[i].type, "recharge") == 0) {
      totalRecharges += transactions[i].amount;
    }
  }
  
  doc["totalSales"] = totalSales;
  doc["totalOrders"] = totalOrders;
  doc["totalRecharges"] = totalRecharges;
  
  // Daily sales (simulated with itemSales data)
  JsonArray daily = doc.createNestedArray("daily");
  for (int i = 0; i < 7; i++) {
    JsonObject d = daily.createNestedObject();
    d["sales"] = dailySales[i];
    d["orders"] = dailyOrders[i];
  }
  
  // Top selling items
  JsonArray topItems = doc.createNestedArray("topItems");
  for (int i = 0; i < menuItemCount && i < 10; i++) {
    if (menuItems[i].active && itemSales[i] > 0) {
      JsonObject item = topItems.createNestedObject();
      item["key"] = menuItems[i].key;
      item["name"] = menuItems[i].name;
      item["qty"] = itemSales[i];
      item["revenue"] = itemSales[i] * menuItems[i].price;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetInventoryReport() {
  DynamicJsonDocument doc(2048);
  JsonArray items = doc.createNestedArray("items");
  
  for (int i = 0; i < menuItemCount; i++) {
    if (menuItems[i].active) {
      JsonObject item = items.createNestedObject();
      item["key"] = menuItems[i].key;
      item["name"] = menuItems[i].name;
      item["stock"] = menuItems[i].stock;
      item["sold"] = itemSales[i];
      item["revenue"] = itemSales[i] * menuItems[i].price;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetUserActivityReport() {
  DynamicJsonDocument doc(2048);
  JsonArray topUsers = doc.createNestedArray("topUsers");
  
  // Calculate spending per user from transactions
  struct UserStats {
    char uid[20];
    char name[20];
    float spent;
    int orders;
  };
  UserStats stats[MAX_USERS];
  int statsCount = 0;
  
  for (int i = 0; i < transactionCount; i++) {
    if (!transactions[i].active || strcmp(transactions[i].type, "order") != 0) continue;
    
    // Find or create stats entry
    int idx = -1;
    for (int j = 0; j < statsCount; j++) {
      if (strcmp(stats[j].uid, transactions[i].uid) == 0) {
        idx = j;
        break;
      }
    }
    
    if (idx < 0 && statsCount < MAX_USERS) {
      idx = statsCount++;
      strcpy(stats[idx].uid, transactions[i].uid);
      strcpy(stats[idx].name, transactions[i].userName);
      stats[idx].spent = 0;
      stats[idx].orders = 0;
    }
    
    if (idx >= 0) {
      stats[idx].spent += transactions[i].amount;
      stats[idx].orders++;
    }
  }
  
  // Sort by spending (simple bubble sort)
  for (int i = 0; i < statsCount - 1; i++) {
    for (int j = i + 1; j < statsCount; j++) {
      if (stats[j].spent > stats[i].spent) {
        UserStats temp = stats[i];
        stats[i] = stats[j];
        stats[j] = temp;
      }
    }
  }
  
  // Output top users
  for (int i = 0; i < statsCount && i < 10; i++) {
    JsonObject user = topUsers.createNestedObject();
    user["uid"] = stats[i].uid;
    user["name"] = stats[i].name;
    user["spent"] = stats[i].spent;
    user["orders"] = stats[i].orders;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Process serial data from Arduino
void processSerialData() {
  static String buffer = "";
  static bool inOrder = false;
  static String orderUID = "";
  static CartItem orderItems[MAX_CART_ITEMS];
  static int orderItemCount = 0;
  static float newBalance = 0;
  static float newCreditUsed = 0;
  
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n') {
      buffer.trim();
      
      if (buffer.startsWith("UID:")) {
        String uid = buffer.substring(4);
        uid.trim();

        if (webScanMode != SCAN_NONE) {
          uid.toCharArray(lastScannedUID, sizeof(lastScannedUID));
          webScanMode = SCAN_NONE;
        }
        
        if (inOrder) {
          uid.toCharArray(orderUID.begin(), 20);
        } else {
          // Auth request
          int idx = findUserByUID(uid.c_str());
          if (systemLocked) {
            // System is locked - deny all access
            uid.toCharArray(lastScannedUID, 20);
            Serial.print("LOCKED,");
            Serial.println(lockMessage);
          } else if (idx >= 0 && users[idx].active) {
            Serial.print("VALID,");
            Serial.print(users[idx].name);
            Serial.print(",");
            Serial.print(users[idx].balance, 2);
            Serial.print(",");
            Serial.print(users[idx].creditUsed, 2);
            Serial.print(",");
            Serial.println(users[idx].creditLimit, 2);
          } else {
            // Store scanned UID for add user via scan feature
            uid.toCharArray(lastScannedUID, 20);
            Serial.println("INVALID");
          }
        }
      }
      else if (buffer == "MENU") {
        // Send menu items
        for (int i = 0; i < menuItemCount; i++) {
          if (menuItems[i].active) {
            Serial.print("ITEM,");
            Serial.print(menuItems[i].key);
            Serial.print(",");
            Serial.print(menuItems[i].name);
            Serial.print(",1,");  // qty placeholder
            Serial.print(menuItems[i].price, 2);
            Serial.print(",");
            Serial.println(menuItems[i].stock);
            
            // Wait for ACK
            unsigned long start = millis();
            while (millis() - start < 1000) {
              if (Serial.available()) {
                String ack = Serial.readStringUntil('\n');
                if (ack.indexOf("ACK") >= 0) break;
              }
              yield();
            }
          }
        }
        Serial.println("MENU_END");
      }
      else if (buffer == "ORDER_START") {
        inOrder = true;
        orderUID = "";
        orderItemCount = 0;
        newBalance = 0;
        newCreditUsed = 0;
      }
      else if (buffer.startsWith("ITEM:") && inOrder) {
        String key = buffer.substring(5);
        key.toCharArray(orderItems[orderItemCount].key, 4);
      }
      else if (buffer.startsWith("QTY:") && inOrder) {
        orderItems[orderItemCount].qty = buffer.substring(4).toInt();
        orderItemCount++;
      }
      else if (buffer.startsWith("BALANCE:") && inOrder) {
        newBalance = buffer.substring(8).toFloat();
      }
      else if (buffer.startsWith("CREDIT_USED:") && inOrder) {
        newCreditUsed = buffer.substring(12).toFloat();
      }
      else if (buffer == "ORDER_END" && inOrder) {
        inOrder = false;
        
        // Process order
        int userIdx = findUserByUID(orderUID.c_str());
        if (userIdx >= 0) {
          // Calculate total and check stock
          float total = 0;
          bool stockOk = true;
          
          for (int i = 0; i < orderItemCount; i++) {
            int menuIdx = findMenuItemByKey(orderItems[i].key);
            if (menuIdx >= 0) {
              if (menuItems[menuIdx].stock < orderItems[i].qty) {
                stockOk = false;
                break;
              }
              total += menuItems[menuIdx].price * orderItems[i].qty;
            }
          }
          
          if (!stockOk) {
            Serial.println("ORDER_FAIL,Insufficient stock");
          } else {
            // Create order
            if (orderCount < MAX_ORDERS) {
              Order& newOrder = orders[orderCount];
              newOrder.id = nextOrderId++;
              strcpy(newOrder.uid, orderUID.c_str());
              strcpy(newOrder.userName, users[userIdx].name);
              memcpy(newOrder.items, orderItems, sizeof(CartItem) * orderItemCount);
              newOrder.itemCount = orderItemCount;
              newOrder.total = total;
              strcpy(newOrder.status, "pending");
              getCurrentTimeStr(newOrder.timeStr);
              newOrder.timestamp = millis();
              newOrder.active = true;
              orderCount++;
              
              // Update user balance
              users[userIdx].balance = newBalance;
              users[userIdx].creditUsed = newCreditUsed;
              
              // Update stock and check for low stock alerts
              for (int i = 0; i < orderItemCount; i++) {
                int menuIdx = findMenuItemByKey(orderItems[i].key);
                if (menuIdx >= 0) {
                  menuItems[menuIdx].stock -= orderItems[i].qty;
                  itemSales[menuIdx] += orderItems[i].qty;  // Track sales
                  checkAndCreateStockAlert(menuIdx);
                }
              }
              
              // Record transaction and update daily sales
              addTransaction(newOrder.id, newOrder.uid, newOrder.userName, total, "order");
              dailySales[0] += total;
              dailyOrders[0]++;
              
              saveData();;
              
              Serial.print("ORDER_OK,");
              Serial.println(newOrder.id);
            } else {
              Serial.println("ORDER_FAIL,Order queue full");
            }
          }
        } else {
          Serial.println("ORDER_FAIL,User not found");
        }
      }
      else if (buffer.startsWith("ONLINE:")) {
        String uid = buffer.substring(7);
        
        // Update online status
        bool found = false;
        for (int i = 0; i < onlineCount; i++) {
          if (strcmp(onlineUsers[i], uid.c_str()) == 0) {
            onlineTimestamps[i] = millis();
            found = true;
            break;
          }
        }
        
        if (!found && onlineCount < MAX_USERS) {
          strcpy(onlineUsers[onlineCount], uid.c_str());
          onlineTimestamps[onlineCount] = millis();
          onlineCount++;
        }
      }
      else if (buffer.startsWith("OFFLINE:")) {
        String uid = buffer.substring(8);
        
        for (int i = 0; i < onlineCount; i++) {
          if (strcmp(onlineUsers[i], uid.c_str()) == 0) {
            for (int j = i; j < onlineCount - 1; j++) {
              strcpy(onlineUsers[j], onlineUsers[j + 1]);
              onlineTimestamps[j] = onlineTimestamps[j + 1];
            }
            onlineCount--;
            break;
          }
        }
      }
      
      buffer = "";
    } else {
      buffer += c;
    }
  }
}

int findUserByUID(const char* uid) {
  for (int i = 0; i < userCount; i++) {
    if (users[i].active && strcmp(users[i].uid, uid) == 0) {
      return i;
    }
  }
  return -1;
}

int findMenuItemByKey(const char* key) {
  for (int i = 0; i < menuItemCount; i++) {
    if (menuItems[i].active && strcmp(menuItems[i].key, key) == 0) {
      return i;
    }
  }
  return -1;
}

void saveData() {
  // Save user count
  EEPROM.write(USER_START_ADDR, userCount);
  
  // Save users
  int addr = USER_START_ADDR + 1;
  for (int i = 0; i < userCount; i++) {
    EEPROM.put(addr, users[i]);
    addr += sizeof(User);
  }
  
  // Save menu count
  EEPROM.write(MENU_START_ADDR, menuItemCount);
  
  // Save menu items
  addr = MENU_START_ADDR + 1;
  for (int i = 0; i < menuItemCount; i++) {
    EEPROM.put(addr, menuItems[i]);
    addr += sizeof(MenuItem);
  }
  
  // Save next order ID
  EEPROM.put(CONFIG_ADDR, nextOrderId);
  
  EEPROM.commit();
}

void loadData() {
  // Load user count
  userCount = EEPROM.read(USER_START_ADDR);
  if (userCount > MAX_USERS) userCount = 0;
  
  // Load users
  int addr = USER_START_ADDR + 1;
  for (int i = 0; i < userCount; i++) {
    EEPROM.get(addr, users[i]);
    addr += sizeof(User);
  }
  
  // Load menu count
  menuItemCount = EEPROM.read(MENU_START_ADDR);
  if (menuItemCount > MAX_MENU_ITEMS) menuItemCount = 0;
  
  // Load menu items
  addr = MENU_START_ADDR + 1;
  for (int i = 0; i < menuItemCount; i++) {
    EEPROM.get(addr, menuItems[i]);
    addr += sizeof(MenuItem);
  }
  
  // Load next order ID
  EEPROM.get(CONFIG_ADDR, nextOrderId);
  if (nextOrderId < 1) nextOrderId = 1;
}
