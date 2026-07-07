#ifndef AUTH_TEMPLATES_H
#define AUTH_TEMPLATES_H

#include <Arduino.h>

// =============================================================================
// 认证相关页面模板：登录页 + 首次账户设置页
// =============================================================================

// 登录页面 - 已设置凭证但未登录时显示
const char AUTH_LOGIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>登录 / Login - UPS 控制中心</title>
<style>
body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:#f0f2f5;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}
.card{background:#fff;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1);padding:32px;width:340px;max-width:90vw}
h2{margin:0 0 4px;font-size:18px;color:#333}
p{font-size:13px;color:#888;margin:0 0 20px}
label{display:block;font-size:13px;color:#555;margin:12px 0 4px}
input{width:100%;box-sizing:border-box;padding:8px 10px;border:1px solid #d9d9d9;border-radius:4px;font-size:14px}
input:focus{border-color:#1677ff;outline:none}
button{width:100%;margin-top:20px;padding:10px;background:#1677ff;color:#fff;border:none;border-radius:4px;font-size:14px;cursor:pointer}
button:hover{background:#4096ff}
button:disabled{background:#91caff;cursor:not-allowed}
.msg{margin-top:12px;font-size:13px;text-align:center;display:none}
.err{color:#f5222d}
</style>
</head>
<body>
<div class="card">
<h2>🔋 UPS 控制中心</h2>
<p>请登录以继续 / Please log in to continue</p>
<label>用户名 / Username</label>
<input type="text" id="u" maxlength="32" autocomplete="username" autofocus>
<label>密码 / Password</label>
<input type="password" id="p" maxlength="64" autocomplete="current-password">
<button id="btn" onclick="login()">登录 / Login</button>
<div class="msg" id="m"></div>
</div>
<script>
document.getElementById('p').addEventListener('keydown',function(e){if(e.key==='Enter')login()});
function login(){
var u=document.getElementById('u').value.trim(),p=document.getElementById('p').value,m=document.getElementById('m'),b=document.getElementById('btn');
m.style.display='block';m.className='msg err';
if(!u||!p){m.textContent='请输入用户名和密码 / Username and password required';return}
b.disabled=true;m.textContent='';
fetch('/api/auth/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:u,password:p})})
.then(function(r){return r.json().then(function(j){return{ok:r.ok,status:r.status,j:j}})})
.then(function(r){
if(r.ok&&r.j.success){location.href='/'}
else{b.disabled=false;m.textContent=r.j.message||(r.status===429?'尝试次数过多，请稍后再试 / Too many attempts':'用户名或密码错误 / Invalid credentials')}
}).catch(function(){b.disabled=false;m.textContent='网络错误 / Network error'})
}
</script>
</body>
</html>
)rawliteral";

// 账户设置页面 - 正常运行模式下凭证为空时（如旧固件升级后）强制显示
const char AUTH_SETUP_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>设置访问账户 / Set Access Account</title>
<style>
body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:#f0f2f5;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}
.card{background:#fff;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1);padding:32px;width:360px;max-width:90vw}
h2{margin:0 0 8px;font-size:18px;color:#333}
p{font-size:13px;color:#888;margin:0 0 20px;line-height:1.6}
label{display:block;font-size:13px;color:#555;margin:12px 0 4px}
input{width:100%;box-sizing:border-box;padding:8px 10px;border:1px solid #d9d9d9;border-radius:4px;font-size:14px}
input:focus{border-color:#1677ff;outline:none}
button{width:100%;margin-top:20px;padding:10px;background:#1677ff;color:#fff;border:none;border-radius:4px;font-size:14px;cursor:pointer}
button:hover{background:#4096ff}
.msg{margin-top:12px;font-size:13px;text-align:center;display:none}
.err{color:#f5222d}.ok{color:#52c41a}
</style>
</head>
<body>
<div class="card">
<h2>🔐 设置访问账户</h2>
<p>为保护设备安全，必须先设置管理账户和密码才能访问本设备。<br>Set an admin account before accessing this device.</p>
<label>用户名 / Username (1-32)</label>
<input type="text" id="u" maxlength="32" autocomplete="username">
<label>密码 / Password (8-64)</label>
<input type="password" id="p" maxlength="64" autocomplete="new-password">
<label>确认密码 / Confirm Password</label>
<input type="password" id="p2" maxlength="64" autocomplete="new-password">
<button onclick="s()">保存并登录 / Save &amp; Login</button>
<div class="msg" id="m"></div>
</div>
<script>
function s(){
var u=document.getElementById('u').value.trim(),p=document.getElementById('p').value,p2=document.getElementById('p2').value,m=document.getElementById('m');
m.style.display='block';m.className='msg err';
if(!u||u.length<1){m.textContent='请输入用户名 / Username required';return}
if(p.length<8){m.textContent='密码至少 8 位 / Password min 8 chars';return}
if(p!==p2){m.textContent='两次密码不一致 / Passwords do not match';return}
fetch('/api/auth/setup',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:u,password:p})})
.then(function(r){return r.json()})
.then(function(r){
if(r.success){m.className='msg ok';m.textContent='设置成功，正在跳转... / Saved, redirecting...';setTimeout(function(){location.href='/'},1000)}
else{m.textContent=r.message||'设置失败 / Failed'}
}).catch(function(){m.textContent='网络错误 / Network error'})
}
</script>
</body>
</html>
)rawliteral";

#endif // AUTH_TEMPLATES_H
