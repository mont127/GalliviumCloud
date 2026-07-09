#include "dashboard.hpp"

// Self-contained OCLI web dashboard.
// One HTML document: inline <style> + <script>, no external assets.
// Talks to the server defined in src/dashboard.cpp over the FIXED CONTRACT
// endpoints (/api/info, /api/history, /api/chat) plus the shared-terminal
// endpoints (/api/term/stream SSE, /api/term/input, /api/term/resize).
// The right panel is a real xterm.js terminal bound to the persistent PTY
// shell that both the user and the AI type into.
//
// NOTE: the raw-string delimiter is DASH; the body must never contain the
// sequence )<DASH>.

namespace ocli {

const char* DASHBOARD_HTML = R"DASH(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OCLI &middot; control dashboard</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:ital,wght@0,400;0,500;0,600;0,700;0,800;1,400&display=swap">
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css">
<script src="https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js"></script>
<style>
  :root{
    --bg:#020617;
    --grid:rgba(148,163,184,.05);
    --green:#22c55e;
    --green-bright:#4ade80;
    --blue:#22c55e;          /* accent unified to green for a cohesive coding-tool identity */
    --blue-bright:#4ade80;
    --amber:#f2c14e;
    --panel:#0b1220;
    --panel-2:#0a0f1c;
    --panel-3:#0c1512;
    --line:rgba(148,163,184,.14);
    --ink:#f8fafc;
    --ink-dim:#94a3b8;
    --ink-faint:#64748b;
    --accent-glow:rgba(34,197,94,.30);
    --radius:14px;
    --radius-sm:10px;
    --mono:"JetBrains Mono",ui-monospace,"SF Mono",Menlo,Consolas,"Roboto Mono",monospace;
  }
  *{box-sizing:border-box}
  html,body{height:100%}
  body{
    margin:0;
    color:var(--ink);
    font-family:var(--mono);
    font-size:14px;
    letter-spacing:.01em;
    background:
      radial-gradient(1100px 560px at 12% -8%, rgba(34,197,94,.11), transparent 55%),
      radial-gradient(920px 520px at 108% 112%, rgba(34,197,94,.07), transparent 52%),
      var(--bg);
    background-attachment:fixed;
    -webkit-font-smoothing:antialiased;
  }
  .mono{font-family:var(--mono)}
  ::selection{background:rgba(34,197,94,.30);color:#eafff2}
  a:focus-visible,button:focus-visible,textarea:focus-visible{
    outline:2px solid var(--green-bright);outline-offset:2px;border-radius:8px;
  }
  @media (prefers-reduced-motion:reduce){
    *{animation-duration:.001ms !important;transition-duration:.001ms !important;scroll-behavior:auto !important}
  }

  /* ---- top-level responsive grid ---- */
  .app{
    height:100vh;
    min-height:100vh;
    padding:18px;
    display:grid;
    gap:18px;
    grid-template-columns:minmax(0,2.1fr) minmax(0,1fr);
    grid-template-rows:auto minmax(0,1fr);
    grid-template-areas:
      "head head"
      "main term";
  }
  @media (max-width:980px){
    .app{
      height:auto;
      min-height:100vh;
      grid-template-columns:1fr;
      grid-template-rows:auto auto auto;
      grid-template-areas:
        "head"
        "main"
        "term";
    }
    .main,.term{min-height:62vh}
  }

  /* ---- header ---- */
  .head{
    grid-area:head;
    display:flex;
    align-items:center;
    gap:16px;
    flex-wrap:wrap;
  }
  .pill{
    display:inline-flex;
    align-items:center;
    gap:10px;
    background:linear-gradient(135deg,var(--green-bright),var(--green));
    color:#04140a;
    font-weight:800;
    letter-spacing:.24em;
    font-size:14px;
    padding:11px 20px;
    border-radius:12px;
    box-shadow:0 0 0 1px rgba(74,222,128,.45), 0 10px 34px var(--accent-glow);
  }
  .pill .dot{
    width:9px;height:9px;border-radius:50%;
    background:#04140a;
    box-shadow:0 0 0 3px rgba(4,20,10,.28);
    animation:pulseDot 1.8s ease-in-out infinite;
  }
  @keyframes pulseDot{0%,100%{opacity:.5;transform:scale(.88)}50%{opacity:1;transform:scale(1.18)}}
  .subtitle{color:var(--ink-dim);font-size:13px;letter-spacing:.02em}
  .meta{
    margin-left:auto;
    display:flex;
    align-items:center;
    gap:8px;
    flex-wrap:wrap;
    justify-content:flex-end;
  }
  .chip{
    display:inline-flex;align-items:center;gap:7px;
    font-size:12px;
    color:var(--ink-dim);
    background:rgba(148,163,184,.06);
    border:1px solid var(--line);
    padding:7px 12px;
    border-radius:10px;
    white-space:nowrap;
    transition:border-color .16s ease, background .16s ease, transform .16s ease;
  }
  .chip:hover{transform:translateY(-1px);border-color:rgba(34,197,94,.4)}
  .chip.on{border-color:rgba(34,197,94,.42);background:rgba(34,197,94,.10)}
  .chip b{color:var(--ink);font-weight:600}
  .chip .k{color:var(--ink-faint);text-transform:uppercase;letter-spacing:.08em;font-size:10px}
  .chip.on b{color:var(--green-bright)}
  .chip a{color:var(--blue-bright);text-decoration:none}
  .chip a:hover{text-decoration:underline}

  /* ---- main conversation panel ---- */
  .main{
    grid-area:main;
    min-height:0;
    position:relative;
    display:flex;
    flex-direction:column;
    background:linear-gradient(180deg,var(--panel),var(--panel-2));
    border:1px solid var(--line);
    border-radius:var(--radius);
    box-shadow:0 24px 54px rgba(0,0,0,.45);
    overflow:hidden;
  }
  .main::before,.term::before{
    content:"";position:absolute;left:0;right:0;top:0;height:1px;
    background:linear-gradient(90deg,transparent,rgba(34,197,94,.55),transparent);
  }
  .main-head{
    display:flex;align-items:center;gap:10px;
    padding:16px 20px;
    border-bottom:1px solid var(--line);
  }
  .main-head .glyph{color:var(--blue);font-size:15px}
  .main-head .t{font-size:14px;font-weight:600;letter-spacing:.01em}
  .main-head .s{margin-left:auto;color:var(--ink-faint);font-size:12px}

  .stream{
    flex:1 1 auto;
    min-height:0;
    overflow-y:auto;
    padding:18px 20px 8px;
    display:flex;
    flex-direction:column;
    gap:14px;
    scroll-behavior:smooth;
  }
  .msg{display:flex;flex-direction:column;gap:6px;max-width:92%}
  .msg .who{
    font-size:10px;letter-spacing:.14em;text-transform:uppercase;
    color:var(--ink-faint);
  }
  .bubble{
    padding:12px 15px;
    border-radius:16px;
    line-height:1.55;
    font-size:14px;
    white-space:pre-wrap;
    word-break:break-word;
  }
  .msg.user{align-self:flex-end;align-items:flex-end}
  .msg.user .bubble{
    background:rgba(34,197,94,.13);
    border:1px solid rgba(34,197,94,.32);
    color:var(--ink);
  }
  .msg.assistant{align-self:flex-start}
  .msg.assistant .who{color:var(--blue)}
  .msg.assistant .bubble{
    background:rgba(12,21,16,.9);
    border:1px solid var(--line);
    border-left:2px solid rgba(34,197,94,.5);
    color:var(--ink);
    font-family:var(--mono);
    font-size:13px;
    line-height:1.6;
  }
  .empty{color:var(--ink-faint);font-size:13px;margin:auto;text-align:center;padding:30px}

  .typing{
    align-self:flex-start;
    display:none;
    align-items:center;gap:8px;
    color:var(--blue);font-size:12px;
    padding:4px 2px;
  }
  .typing.show{display:flex}
  .typing .ds{display:inline-flex;gap:4px}
  .typing .ds i{
    width:7px;height:7px;border-radius:50%;background:var(--blue);
    animation:blink 1.2s infinite ease-in-out;
  }
  .typing .ds i:nth-child(2){animation-delay:.2s}
  .typing .ds i:nth-child(3){animation-delay:.4s}
  @keyframes blink{0%,80%,100%{opacity:.25;transform:translateY(0)}40%{opacity:1;transform:translateY(-3px)}}

  /* ---- green input bar pinned at bottom of main panel ---- */
  .composer{
    margin:12px;
    padding:9px 9px 9px 16px;
    display:flex;
    align-items:flex-end;
    gap:10px;
    background:linear-gradient(180deg,var(--green-bright),var(--green));
    border-radius:var(--radius);
    box-shadow:0 12px 34px var(--accent-glow), inset 0 0 0 1px rgba(255,255,255,.14);
    transition:box-shadow .18s ease, transform .18s ease;
  }
  .composer:focus-within{box-shadow:0 14px 40px var(--accent-glow), inset 0 0 0 1px rgba(255,255,255,.3), 0 0 0 3px rgba(34,197,94,.25)}
  .composer textarea{
    flex:1 1 auto;
    resize:none;
    border:none;
    outline:none;
    background:transparent;
    color:#04140a;
    font:inherit;
    font-size:14px;
    line-height:1.45;
    max-height:140px;
    min-height:24px;
    padding:8px 0;
  }
  .composer textarea::placeholder{color:rgba(4,20,10,.62)}
  .send{
    flex:0 0 auto;
    border:none;
    cursor:pointer;
    background:#04140a;
    color:var(--blue-bright);
    font-weight:700;
    font-size:13px;
    letter-spacing:.04em;
    padding:11px 18px;
    border-radius:13px;
    transition:transform .08s ease, opacity .15s ease;
  }
  .send:hover{transform:translateY(-1px)}
  .send:active{transform:translateY(0)}
  .send:disabled{opacity:.55;cursor:default;transform:none}

  /* ---- green right-side interactive terminal panel ---- */
  .term{
    grid-area:term;
    min-height:0;
    position:relative;
    display:flex;
    flex-direction:column;
    background:linear-gradient(180deg,var(--panel-3),#080f0b);
    border:1px solid rgba(34,197,94,.28);
    border-radius:var(--radius);
    box-shadow:0 24px 54px rgba(0,0,0,.45), inset 0 0 0 1px rgba(34,197,94,.05);
    overflow:hidden;
  }
  .term-head{
    display:flex;align-items:center;gap:9px;
    padding:14px 16px;
    color:#eafaea;
    border-bottom:1px solid rgba(0,0,0,.25);
    background:rgba(0,0,0,.12);
  }
  .term-head .lights{display:inline-flex;gap:6px}
  .term-head .lights i{width:10px;height:10px;border-radius:50%;background:rgba(255,255,255,.35)}
  .term-head .lights i:first-child{background:#ffd36a}
  .term-head .lights i:last-child{background:#8ef0a0}
  .term-head .t{font-size:13px;font-weight:600;letter-spacing:.02em}
  .term-head .s{margin-left:auto;font-size:11px;color:rgba(234,250,234,.7)}

  /* ---- shared xterm.js terminal mount ---- */
  .term-xterm{
    flex:1 1 auto;
    min-height:0;
    margin:10px;
    padding:8px 10px;
    background:#050805;
    border:1px solid rgba(103,185,106,.30);
    border-radius:var(--radius-sm);
    overflow:hidden;
  }
  .term-xterm .xterm{height:100%;padding:0}
  .term-xterm .xterm-viewport{background-color:transparent !important;overflow-y:auto !important}
  .term-xterm .xterm-viewport::-webkit-scrollbar{width:10px}
  .term-xterm .xterm-viewport::-webkit-scrollbar-thumb{
    background:rgba(103,185,106,.25);border-radius:999px;border:3px solid transparent;background-clip:padding-box;
  }

  /* scrollbars */
  .stream::-webkit-scrollbar{width:10px}
  .stream::-webkit-scrollbar-thumb{
    background:rgba(103,185,106,.25);border-radius:999px;border:3px solid transparent;background-clip:padding-box;
  }
</style>
</head>
<body>
  <div class="app">

    <header class="head">
      <span class="pill"><span class="dot"></span>OCLI</span>
      <span class="subtitle">AI agent &middot; LAN control dashboard</span>
      <div class="meta" id="meta">
        <span class="chip"><span class="k">model</span><b id="m-model">&hellip;</b></span>
        <span class="chip"><span class="k">backend</span><b id="m-backend">&hellip;</b></span>
        <span class="chip" id="m-auto-chip"><span class="k">auto</span><b id="m-auto">&hellip;</b></span>
        <span class="chip" id="m-tools-chip"><span class="k">tools</span><b id="m-tools">&hellip;</b></span>
        <span class="chip"><span class="k">lan</span><a id="m-lan" href="#">&hellip;</a></span>
      </div>
    </header>

    <section class="main">
      <div class="main-head">
        <span class="glyph">&#9678;</span>
        <span class="t">Tool calls and AI answering</span>
        <span class="s" id="main-status">connecting&hellip;</span>
      </div>
      <div class="stream" id="stream">
        <div class="empty" id="empty">No conversation yet. Send the first prompt below.</div>
        <div class="typing" id="typing">
          <span class="ds"><i></i><i></i><i></i></span>
          <span>OCLI is working&hellip;</span>
        </div>
      </div>
      <form class="composer" id="composer" autocomplete="off">
        <textarea id="prompt" rows="1" placeholder="Follow up prompts / first prompts&hellip;"></textarea>
        <button class="send" id="send" type="submit">Send</button>
      </form>
    </section>

    <aside class="term">
      <div class="term-head">
        <span class="lights"><i></i><i></i><i></i></span>
        <span class="t">Shared terminal &middot; you and the AI type here</span>
        <span class="s">/api/term</span>
      </div>
      <div class="term-xterm" id="termXterm"></div>
    </aside>

  </div>

<script>
(function(){
  "use strict";

  var stream   = document.getElementById("stream");
  var emptyEl  = document.getElementById("empty");
  var typing   = document.getElementById("typing");
  var composer = document.getElementById("composer");
  var prompt   = document.getElementById("prompt");
  var sendBtn  = document.getElementById("send");
  var mainStat = document.getElementById("main-status");

  var termHost = document.getElementById("termXterm");

  var busy = false;          // chat turn in flight
  var lastHistory = "";      // last rendered history signature

  function nearBottom(el){
    return (el.scrollHeight - el.scrollTop - el.clientHeight) < 80;
  }
  function setText(id, v){
    var e = document.getElementById(id);
    if (e) e.textContent = (v === undefined || v === null || v === "") ? "n/a" : String(v);
  }

  /* ---------- header info ---------- */
  function loadInfo(){
    fetch("/api/info").then(function(r){ return r.json(); }).then(function(d){
      setText("m-model",   d.model);
      setText("m-backend", d.backend);
      var autoOn = !!d.auto;
      setText("m-auto", autoOn ? "on" : "off");
      document.getElementById("m-auto-chip").classList.toggle("on", autoOn);
      var tools = (d.tool_access === undefined || d.tool_access === null) ? "n/a" : String(d.tool_access);
      setText("m-tools", tools);
      var toolsOn = /full|on|all|enabled|true|yes/i.test(tools);
      document.getElementById("m-tools-chip").classList.toggle("on", toolsOn);
      var lan = document.getElementById("m-lan");
      if (d.lan_url){ lan.textContent = d.lan_url; lan.href = d.lan_url; }
      else { lan.textContent = "n/a"; lan.removeAttribute("href"); }
    }).catch(function(){ /* keep last values */ });
  }

  /* ---------- conversation ---------- */
  function renderHistory(messages){
    var sig = JSON.stringify(messages);
    if (sig === lastHistory) return;   // nothing changed, avoid flicker
    lastHistory = sig;

    var stick = nearBottom(stream);

    // wipe everything except the persistent typing indicator
    var node = stream.firstChild;
    while (node){
      var next = node.nextSibling;
      if (node !== typing) stream.removeChild(node);
      node = next;
    }

    if (!messages || !messages.length){
      stream.insertBefore(emptyEl, typing);
    } else {
      for (var i = 0; i < messages.length; i++){
        var m = messages[i] || {};
        var role = (m.role === "user") ? "user" : "assistant";
        var wrap = document.createElement("div");
        wrap.className = "msg " + role;

        var who = document.createElement("div");
        who.className = "who";
        who.textContent = (role === "user") ? "You" : "OCLI";

        var bub = document.createElement("div");
        bub.className = "bubble";
        bub.textContent = (m.content === undefined || m.content === null) ? "" : String(m.content);

        wrap.appendChild(who);
        wrap.appendChild(bub);
        stream.insertBefore(wrap, typing);
      }
    }
    if (stick) stream.scrollTop = stream.scrollHeight;
  }

  function loadHistory(){
    return fetch("/api/history").then(function(r){ return r.json(); }).then(function(d){
      renderHistory((d && d.messages) ? d.messages : []);
    }).catch(function(){
      mainStat.textContent = "offline";
    });
  }

  function showTyping(on){
    var stick = nearBottom(stream);
    typing.classList.toggle("show", on);
    if (on){
      stream.appendChild(typing);     // keep it last
    }
    if (stick) stream.scrollTop = stream.scrollHeight;
  }

  function setBusy(on){
    busy = !!on;
    sendBtn.disabled = busy;
    composer.classList.toggle("busy", busy);
    showTyping(busy);
  }

  function loadStatus(){
    return fetch("/api/status").then(function(r){ return r.json(); }).then(function(d){
      var on = !!(d && d.busy);
      setBusy(on);
      return loadHistory().then(function(){
        if (on) mainStat.textContent = "working";
        else if (d && d.status === "error") mainStat.textContent = "error";
        else mainStat.textContent = "live";
      });
    }).catch(function(){
      mainStat.textContent = "offline";
    });
  }

  function sendChat(){
    var text = prompt.value.trim();
    if (!text || busy) return;
    setBusy(true);
    prompt.value = "";
    autoGrow();
    lastHistory = "";          // force a re-render so the user msg shows now
    mainStat.textContent = "queued";

    fetch("/api/chat", {
      method:"POST",
      headers:{ "Content-Type":"application/json" },
      body: JSON.stringify({ message: text })
    }).then(function(r){
        if (!r.ok){
          return r.json().catch(function(){ return {}; }).then(function(d){
            throw new Error((d && d.error) ? d.error : "request failed");
          });
        }
        return r.json();
      })
      .then(function(){ return loadStatus(); })
      .catch(function(err){
        setBusy(false);
        mainStat.textContent = err && err.message ? err.message : "error";
      })
      .then(function(){
        prompt.focus();
      });
  }

  /* ---------- shared interactive terminal (xterm.js) ---------- */
  /* One persistent PTY-backed shell that BOTH this panel and the AI type into.
     SSE pushes the live PTY output (snapshot first, then deltas); keystrokes
     and the AI's commands all land in the same TTY. */
  var term = null;
  var fitAddon = null;

  function pushResize(){
    if (!term || !term.rows || !term.cols) return;
    fetch("/api/term/resize", {
      method:"POST",
      headers:{ "Content-Type":"application/json" },
      body: JSON.stringify({ rows: term.rows, cols: term.cols })
    }).catch(function(){ /* server may be busy; harmless */ });
  }

  function fitTerm(){
    if (!fitAddon) return;
    try { fitAddon.fit(); } catch(e){ /* container not laid out yet */ }
    pushResize();
  }

  function installWheelScrollGuard(){
    if (!term) return;
    var wheelTarget = termHost.querySelector(".xterm-viewport") || termHost;
    var handler = function(e){
      // xterm can translate wheel movement into mouse-report escape bytes when
      // an app enables mouse tracking. The dashboard should scroll history.
      var dy = e.deltaY || 0;
      var px = (e.deltaMode === 1) ? 1 : (e.deltaMode === 2 ? term.rows : 32);
      var lines = Math.max(1, Math.min(20, Math.ceil(Math.abs(dy) / px)));
      try { term.scrollLines((dy > 0 ? 1 : -1) * lines); } catch(err){}
      e.preventDefault();
      e.stopPropagation();
      if (e.stopImmediatePropagation) e.stopImmediatePropagation();
      return false;
    };
    termHost.addEventListener("wheel", handler, { passive:false, capture:true });
    if (wheelTarget !== termHost){
      wheelTarget.addEventListener("wheel", handler, { passive:false, capture:true });
    }
  }

  function stripWheelMouseReports(data){
    if (!data) return data;
    var out = "";
    for (var i = 0; i < data.length; ){
      if (data.charCodeAt(i) === 27 && data[i + 1] === "["){
        // SGR mouse mode: ESC [ < button ; x ; y M/m. Wheel uses button bit 64.
        if (data[i + 2] === "<"){
          var j = i + 3;
          var button = "";
          while (j < data.length && data[j] >= "0" && data[j] <= "9") button += data[j++];
          if (data[j] === ";"){
            var k = j + 1;
            while (k < data.length && data[k] !== "M" && data[k] !== "m") k++;
            if (k < data.length){
              var b = parseInt(button, 10);
              if (!isNaN(b) && (b & 64)){
                i = k + 1;
                continue;
              }
            }
          }
        }
        // Legacy X10 mouse mode: ESC [ M cb cx cy.
        if (data[i + 2] === "M" && i + 5 < data.length){
          var legacyButton = data.charCodeAt(i + 3) - 32;
          if (legacyButton & 64){
            i += 6;
            continue;
          }
        }
      }
      out += data[i++];
    }
    return out;
  }

  function setupTerminal(){
    if (typeof Terminal === "undefined"){
      // xterm CDN not loaded yet (slow link); retry without blocking the page.
      setTimeout(setupTerminal, 800);
      return;
    }
    term = new Terminal({
      cursorBlink:true,
      convertEol:false,
      scrollback:20000,
      scrollSensitivity:3,
      fastScrollModifier:"alt",
      fastScrollSensitivity:8,
      smoothScrollDuration:80,
      fontFamily:'"SF Mono",ui-monospace,Menlo,Consolas,"Roboto Mono",monospace',
      fontSize:12.5,
      lineHeight:1.2,
      theme:{
        background:"#060a08",
        foreground:"#8affb0",
        cursor:"#4ade80",
        cursorAccent:"#060a08",
        selectionBackground:"rgba(34,197,94,.35)",
        black:"#0b120c",
        green:"#22c55e",
        brightGreen:"#4ade80"
      }
    });
    if (typeof FitAddon !== "undefined" && FitAddon.FitAddon){
      fitAddon = new FitAddon.FitAddon();
      term.loadAddon(fitAddon);
    }
    term.open(termHost);
    fitTerm();
    setTimeout(fitTerm, 60);
    installWheelScrollGuard();
    termHost.addEventListener("click", function(){ try { term.focus(); } catch(e){} });

    // local keystrokes -> shared PTY master (raw bytes).
    term.onData(function(d){
      d = stripWheelMouseReports(d);
      if (!d) return;
      fetch("/api/term/input", {
        method:"POST",
        headers:{ "Content-Type":"application/json" },
        body: JSON.stringify({ data: d })
      }).catch(function(){ /* dropped keystroke; user can retype */ });
    });

    // shared PTY output (server sends JSON-encoded chunks) -> terminal.
    var es = new EventSource("/api/term/stream");
    es.onmessage = function(e){
      try { term.write(JSON.parse(e.data)); }
      catch(err){ term.write(e.data); }
    };
    es.onerror = function(){ /* EventSource reconnects on its own */ };

    if (window.ResizeObserver){
      try { new ResizeObserver(function(){ fitTerm(); }).observe(termHost); }
      catch(e){ /* older engines fall back to window resize */ }
    }
    window.addEventListener("resize", fitTerm);
  }

  /* ---------- textarea auto-grow + key handling ---------- */
  function autoGrow(){
    prompt.style.height = "auto";
    prompt.style.height = Math.min(prompt.scrollHeight, 140) + "px";
  }
  prompt.addEventListener("input", autoGrow);
  prompt.addEventListener("keydown", function(e){
    if (e.key === "Enter" && !e.shiftKey){
      e.preventDefault();
      sendChat();
    }
  });

  composer.addEventListener("submit", function(e){ e.preventDefault(); sendChat(); });

  /* ---------- boot ---------- */
  loadInfo();
  loadStatus();
  setupTerminal();
  setInterval(loadStatus, 1500);
  setInterval(loadInfo, 15000);
  prompt.focus();
})();
</script>
</body>
</html>
)DASH";

}  // namespace ocli
