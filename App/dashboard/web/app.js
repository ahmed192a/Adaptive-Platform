"use strict";
const $ = (s, r = document) => r.querySelector(s);
const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const api = async (path, body) => {
  const r = await fetch("/api" + path, body === undefined ? {} : { method: "POST", body: JSON.stringify(body) });
  return r.json();
};
const hhmmss = ms => new Date(ms).toLocaleTimeString([], { hour12: false });
const SESSIONS = { 1: "default", 2: "programming", 3: "extended" };

/* ---------------------------------------------------------------- overview */
let logSeq = 0, lastOk = 0;
const beats = {};       // worker name -> array of 0/1 samples
async function poll() {
  try {
    const o = await api("/overview?since=" + logSeq);
    lastOk = Date.now();
    $("#hDot").className = "dot ok"; $("#hNode").textContent = "online";
    $("#hBoot").textContent = o.per.bootCount ?? "–";
    const sess = SESSIONS[o.diag.session] || "?";
    $("#hSession").textContent = sess; $("#diagSess").textContent = sess;
    renderPhm(o.phm);
    renderPer(o.per);
    renderEmState(o.em);
    for (const l of o.logs) { logSeq = Math.max(logSeq, l.seq); addLog(l); }
  } catch (e) {
    $("#hDot").className = "dot bad"; $("#hNode").textContent = "offline";
  }
}

/* --------------------------------------------------------------------- PHM */
const worstOrder = ["OK", "DEACTIVATED", "FAILED", "EXPIRED", "STOPPED"];
function renderPhm(p) {
  $("#wdBanner").classList.toggle("hidden", !p.watchdogFired);
  $("#autoRec").checked = p.autoRecovery;
  const host = $("#workers");
  if (host.children.length !== p.workers.length) {
    host.innerHTML = p.workers.map(w => `
      <div class="worker" id="w-${w.name}">
        <div class="top"><b>${w.name}</b><span class="fg">${w.functionGroup}</span><span class="status"></span></div>
        <svg class="beat" viewBox="0 0 200 34" preserveAspectRatio="none"><polyline points=""/></svg>
        <div class="meta"><span class="el"></span><span class="rs"></span></div>
        <div class="acts"><button class="btn danger" data-act="hang">Inject fault</button><button class="btn" data-act="heal">Clear fault</button></div>
      </div>`).join("");
    host.querySelectorAll("button").forEach(b => b.onclick = async e => {
      const name = e.target.closest(".worker").id.slice(2);
      await api("/phm/" + e.target.dataset.act, { name });
      poll();
    });
  }
  let worst = "OK";
  for (const w of p.workers) {
    const el = $("#w-" + w.name);
    const st = el.querySelector(".status");
    st.textContent = w.global; st.className = "status s-" + w.global;
    el.classList.toggle("hung", w.hung);
    el.querySelector(".el").textContent = "alive supervision: " + w.elementary + (w.hung ? " · fault injected" : "");
    el.querySelector(".rs").textContent = "restarts: " + w.restarts;
    const a = beats[w.name] || (beats[w.name] = []);
    a.push(w.hung ? 0 : 1); if (a.length > 40) a.shift();
    el.querySelector("polyline").setAttribute("points",
      a.map((v, i) => `${(i / 39) * 200},${v ? (i % 2 ? 8 : 26) : 30}`).join(" "));
    if (worstOrder.indexOf(w.global) > worstOrder.indexOf(worst)) worst = w.global;
  }
  const sum = $("#phmSummary"); sum.textContent = p.watchdogFired ? "WATCHDOG" : worst;
  sum.className = "chip " + (p.watchdogFired || worst === "EXPIRED" || worst === "STOPPED" ? "bad" : worst === "FAILED" ? "warn" : "ok");
  const ul = $("#phmEvents");
  const evs = p.events.filter(e => e.from !== "DEACTIVATED" || e.to !== "OK").slice(-30).reverse();
  ul.innerHTML = evs.length ? evs.map(e => `<li><span class="t">${hhmmss(e.t)}</span>${esc(e.name)}: ${esc(e.from)} → <b>${esc(e.to)}</b></li>`).join("")
    : '<li class="muted">no status changes yet</li>';
}
$("#autoRec").onchange = e => api("/phm/auto-recovery", { enabled: e.target.checked });
$("#btnReset").onclick = async () => { await api("/system/reset", {}); poll(); };

/* ------------------------------------------------------------- Persistency */
let perShown = "";
function renderPer(per) {
  const sig = JSON.stringify(per);
  if (sig === perShown) return;
  perShown = sig;
  $("#perTable tbody").innerHTML = Object.entries(per).map(([k, v]) =>
    `<tr><td class="k" data-k="${esc(k)}" data-v="${esc(v)}">${esc(k)}</td><td>${esc(v)}</td><td><button title="delete" data-del="${esc(k)}">✕</button></td></tr>`).join("");
  $("#perTable").querySelectorAll("[data-del]").forEach(b => b.onclick = async () => {
    const r = await api("/per/delete", { key: b.dataset.del }); perMsg(r); perShown = ""; poll();
  });
  $("#perTable").querySelectorAll(".k").forEach(c => c.onclick = () => { $("#perKey").value = c.dataset.k; $("#perVal").value = c.dataset.v; });
}
function perMsg(r) { const m = $("#perMsg"); m.textContent = r.ok ? "persisted (CRC-protected, atomic write)" : "error: " + r.error; m.className = "msg " + (r.ok ? "ok" : "bad"); }
$("#perForm").onsubmit = async e => {
  e.preventDefault();
  const raw = $("#perVal").value.trim(); let v = raw;
  if (raw !== "" && !isNaN(Number(raw))) v = Number(raw); else if (raw === "true" || raw === "false") v = raw === "true";
  perMsg(await api("/per/set", { key: $("#perKey").value.trim(), value: v })); perShown = ""; poll();
};

/* ------------------------------------------------------------- Diagnostics */
const NRC = { 0x10: "generalReject", 0x11: "serviceNotSupported", 0x12: "subFunctionNotSupported", 0x13: "incorrectMessageLengthOrInvalidFormat",
  0x22: "conditionsNotCorrect", 0x31: "requestOutOfRange", 0x33: "securityAccessDenied", 0x72: "generalProgrammingFailure", 0x7F: "serviceNotSupportedInActiveSession" };
const DID = { 0xF190: "VIN", 0xF189: "software version", 0xF1A0: "boot counter", 0xF1A1: "PHM status", 0xF1A2: "vehicle name" };
const QUICK = [["Extended session", "10 03"], ["Default session", "10 01"], ["Tester present", "3E 00"], ["Read VIN", "22 F1 90"], ["Read boot counter", "22 F1 A0"],
  ["Read PHM status", "22 F1 A1"], ["Read vehicle name", "22 F1 A2"], ["Write name 'Bob'", "2E F1 A2 42 6F 62"], ["Release workers", "31 01 FF 00"], ["ECU reset", "11 01"], ["Unknown service", "99"]];
$("#diagQuick").innerHTML = QUICK.map(([n, h]) => `<button class="btn" data-h="${h}">${n}</button>`).join("");
$("#diagQuick").onclick = e => { if (e.target.dataset.h) sendDiag(e.target.dataset.h); };
$("#diagForm").onsubmit = e => { e.preventDefault(); sendDiag($("#diagInput").value); };
const hexAscii = b => b.map(x => x >= 32 && x < 127 ? String.fromCharCode(x) : ".").join("");
function explain(req, resp) {
  if (resp === null) return "no response (positive response suppressed)";
  const r = resp.split(" ").map(h => parseInt(h, 16)); const q = req.split(" ").map(h => parseInt(h, 16));
  if (r[0] === 0x7F) return `negative response to service 0x${q[0].toString(16).toUpperCase()}: ${NRC[r[2]] || "NRC 0x" + r[2].toString(16)}`;
  switch (r[0]) {
    case 0x50: return `session switched to ${SESSIONS[r[1]]} (P2 ${(r[2] << 8) | r[3]} ms)`;
    case 0x7E: return "tester present acknowledged";
    case 0x51: return "ECU reset accepted — the platform reboots";
    case 0x6E: return "data written and persisted";
    case 0x71: return "routine executed";
    case 0x62: { const did = (r[1] << 8) | r[2]; const name = DID[did] || "DID";
      const data = r.slice(3, q.length === 3 ? undefined : 3 + 100);
      if (did === 0xF1A0) return `boot counter = ${(r[3] << 24 | r[4] << 16 | r[5] << 8 | r[6]) >>> 0}`;
      if (did === 0xF1A1) return "worker status: " + r.slice(3, 6).map(x => ["OK", "FAILED", "EXPIRED", "STOPPED", "DEACTIVATED"][x] ?? x).join(", ");
      return `${name}: "${hexAscii(data)}"`; }
  }
  return "";
}
async function sendDiag(hex) {
  if (!hex.trim()) return;
  const r = await api("/diag", { request: hex });
  const c = $("#diagConsole");
  const d = document.createElement("div"); d.className = "entry";
  if (!r.ok) d.innerHTML = `<div class="neg">${esc(r.error)}</div>`;
  else d.innerHTML = `<div class="req">→ ${esc(r.request)}</div><div class="${r.response && r.response.startsWith("7F") ? "neg" : "pos"}">← ${r.response === null ? "(none)" : esc(r.response)}</div><div class="note">${esc(explain(r.request, r.response))}</div>`;
  c.prepend(d); poll();
}

/* ------------------------------------------------------------------ Crypto */
let lastSealed = "", lastSig = "";
$("#cTabs").onclick = e => {
  const t = e.target.dataset.t; if (!t) return;
  $("#cTabs").querySelectorAll("button").forEach(b => b.classList.toggle("on", b === e.target));
  $("#cHash").classList.toggle("hidden", t !== "hash"); $("#cAead").classList.toggle("hidden", t !== "aead"); $("#cSig").classList.toggle("hidden", t !== "sig");
};
async function crypto(op, extra) {
  const r = await api("/crypto/" + op, Object.assign({ text: $("#cText").value, aad: $("#cAad").value, alg: op === "hash" ? $("#cAlg").value : $("#cSigAlg").value }, extra));
  return r;
}
function show(s, bad) { const o = $("#cOut"); o.textContent = s; o.style.color = bad ? "var(--bad)" : "var(--ok)"; }
document.querySelectorAll("#cardCrypto [data-op]").forEach(b => b.onclick = async () => {
  const op = b.dataset.op;
  if (op === "open") { const r = await crypto("open", { hex: lastSealed }); return r.ok ? show("decrypted: " + r.text) : show("AUTHENTICATION FAILED — " + r.error, true); }
  if (op === "verify") { const r = await crypto("verify", { sig: lastSig }); return r.valid ? show("signature VALID") : show("signature INVALID", true); }
  const r = await crypto(op);
  if (!r.ok) return show(r.error, true);
  if (op === "seal") lastSealed = r.hex; if (op === "sign") lastSig = r.hex;
  show(r.hex);
});
$("#cTamper").onclick = async () => {
  if (!lastSealed) return show("press Seal first", true);
  const parts = lastSealed.split(" "); parts[14] = (parseInt(parts[14] || "0", 16) ^ 1).toString(16).padStart(2, "0").toUpperCase();
  const r = await crypto("open", { hex: parts.join(" ") });
  r.ok ? show("decrypted?! " + r.text, true) : show("tampered ciphertext rejected: " + r.error, true);
};
$("#cVerifyTampered").onclick = async () => {
  if (!lastSig) return show("press Sign first", true);
  const r = await api("/crypto/verify", { text: $("#cText").value + "!", sig: lastSig, alg: $("#cSigAlg").value });
  r.valid ? show("valid?!", true) : show("altered message rejected: signature INVALID", true);
};

/* ----------------------------------------------------------------- Logging */
function addLog(l) {
  const v = $("#logView"); const atEnd = v.scrollTop + v.clientHeight >= v.scrollHeight - 8;
  const d = document.createElement("div");
  d.innerHTML = `<span class="t">${hhmmss(l.t)}</span> <span class="lv-${l.level}">${l.level}</span> <span class="t">${esc(l.ctx)}</span> ${esc(l.msg)}`;
  v.appendChild(d); while (v.children.length > 300) v.removeChild(v.firstChild);
  if (atEnd) v.scrollTop = v.scrollHeight;
}
$("#btnClearLog").onclick = () => $("#logView").innerHTML = "";

/* ------------------------------------------------------ Execution management */
let emRunning = false, emLast = "";
function renderEmState(em) {
  emRunning = em.running;
  const c = $("#emState"); c.textContent = em.running ? "running" : "idle"; c.className = "chip " + (em.running ? "ok" : "");
  $("#btnEmStart").disabled = em.running; $("#btnEmStop").disabled = !em.running;
}
$("#btnEmStart").onclick = async () => { emLast = ""; const r = await api("/em/start", {}); if (!r.ok) alert(r.error); poll(); };
$("#btnEmStop").onclick = async () => { await api("/em/stop", {}); poll(); };

function parseEm(text) {
  const fgs = {}, procs = {}, tl = [];
  for (const raw of text.split("\n")) {
    let m;
    if ((m = raw.match(/State Transition \((\w+)\):\s+(\S+)\s+-->\s+(\S+)/))) {
      fgs[m[1]] = m[3]; tl.push({ k: "trans", t: `${m[1]}: ${m[2]} → <b>${m[3]}</b>` });
    } else if ((m = raw.match(/Starting executable "([^"]+)"/))) {
      procs[m[1]] = { st: "starting" }; tl.push({ k: "start", t: `start <b>${m[1]}</b>` });
    } else if ((m = raw.match(/report succeed executing "([^"]+)"\.\s+\[code:\d+, pid:(\d+)\]/))) {
      procs[m[1]] = { st: "running", pid: m[2] };
    } else if ((m = raw.match(/report succeed terminatting (\S+) with code/))) {
      procs[m[1]] = Object.assign(procs[m[1]] || {}, { st: "terminated" }); tl.push({ k: "stop", t: `terminated <b>${m[1]}</b>` });
    }
  }
  return { fgs, procs, tl };
}
async function pollEm() {
  try {
    const files = await api("/em/files");
    const sel = $("#emFile");
    if (files.ok && files.files.join() !== [...sel.options].map(o => o.value).join()) {
      const cur = sel.value;
      sel.innerHTML = files.files.sort().map(f => `<option>${f}</option>`).join("");
      sel.value = files.files.includes(cur) ? cur : (files.files.includes("EM.txt") ? "EM.txt" : files.files[0]);
    }
    const em = await api("/em/log?name=EM.txt");
    if (em.ok && em.text !== emLast) {
      emLast = em.text;
      const p = parseEm(em.text);
      const fg = $("#fgList");
      fg.innerHTML = Object.keys(p.fgs).length ? Object.entries(p.fgs).map(([k, v]) => `<div class="fg">${k} <b>${esc(v)}</b></div>`).join("") : '<span class="muted">start the platform to see state transitions</span>';
      $("#procTable tbody").innerHTML = Object.entries(p.procs).map(([n, x]) => `<tr><td>${esc(n)}</td><td>${x.pid || ""}</td><td><span class="pill ${x.st}">${x.st}</span></td></tr>`).join("");
      const tl = $("#emTimeline");
      tl.innerHTML = p.tl.length ? p.tl.map(e => `<li class="${e.k}">${e.t}</li>`).join("") : '<li class="muted">no events yet</li>';
      tl.scrollTop = tl.scrollHeight;
    }
    if ($("#emFile").value) {
      const f = await api("/em/log?name=" + encodeURIComponent($("#emFile").value));
      const v = $("#emLog");
      if (f.ok && v.dataset.sig !== f.name + f.text.length) { v.dataset.sig = f.name + f.text.length; v.textContent = f.text; v.scrollTop = v.scrollHeight; }
    }
  } catch (e) { /* node offline: poll() shows it */ }
}

setInterval(poll, 400); setInterval(pollEm, 800); poll(); pollEm();
