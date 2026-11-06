"use strict";
/* Dashboard v2 "lab" cards: NM, time sync, diagnostics lab, software update, key slots, demo apps.
   Everything here talks to the real in-process libraries through /api/<area>/<op>. */
const sig = {};                               // last rendered signature per card (avoids flicker)
const changed = (k, v) => { const s = JSON.stringify(v); if (sig[k] === s) return false; sig[k] = s; return true; };
const post = (p, b) => api(p, b || {});

function renderLab(lab) {
  if (!lab) return;
  renderNm(lab.nm); renderTs(lab.tsync); renderUdsx(lab.udsx); renderUpd(lab.update); renderKeys(lab.keys); renderApps(lab.apps);
}

/* ------------------------------------------------------------ Network Management */
function renderNm(nm) {
  $("#nmClock").textContent = "t = " + nm.time + " ms"; $("#nmPdus").textContent = nm.pdus + " PDUs";
  if (changed("nm", nm.ecus)) {
    $("#nmEcus").innerHTML = nm.ecus.map(e => `
      <div class="ecu" data-ecu="${esc(e.name)}"><b>${esc(e.name)}</b>
        <span class="st nm-${e.state}">${e.state}</span>
        <div class="pns">${e.pn.map((on, i) => `<span class="${on ? "on" : ""} ${e.pnMask & (1 << i) ? "" : "off"}" title="partial network ${i}${e.pnMask & (1 << i) ? "" : " (not served by this ECU)"}">PN${i}</span>`).join("")}</div>
        <div class="acts"><button class="btn" data-op="request">${e.requested ? "✓ requested" : "Request"}</button><button class="btn" data-op="release">Release</button>
          ${[0, 1].filter(i => e.pnMask & (1 << i)).map(i => `<button class="btn" data-op="pn${i}">${e.pn[i] ? "Drop" : "Req"} PN${i}</button>`).join("")}</div>
      </div>`).join("");
  }
  if (changed("nmEv", nm.events)) {
    $("#nmEvents").innerHTML = nm.events.length ? nm.events.map(e => `<li>${e.t} ms · <b>${esc(e.ecu)}</b> ${e.from} → ${e.to}</li>`).join("") : '<li class="muted">no state changes yet</li>';
    $("#nmEvents").scrollTop = 1e6;
  }
}
$("#nmEcus").onclick = async e => {
  const op = e.target.dataset.op; if (!op) return;
  const ecu = e.target.closest(".ecu").dataset.ecu;
  const pn = op.startsWith("pn") ? +op.slice(2) : undefined;
  const on = e.target.textContent.startsWith("Req");
  await post("/nm/" + (pn !== undefined ? (on ? "request" : "release") : op), pn !== undefined ? { ecu, pn } : { ecu });
  poll();
};
document.querySelectorAll("[data-nm]").forEach(b => b.onclick = async () => { await post("/nm/advance", { ms: +b.dataset.nm }); poll(); });
$("#nmReset").onclick = async () => { await post("/nm/reset"); sig.nm = sig.nmEv = ""; poll(); };

/* ------------------------------------------------------------------ Time sync */
function renderTs(t) {
  if (!t.available) return;
  const chip = $("#tsStatus"); chip.textContent = t.status;
  chip.className = "chip " + (t.status === "Synchronized" ? "ok" : t.status === "TimeOut" ? "bad" : "warn");
  $("#tsFlags").textContent = "leap: " + t.leap + (t.rateCorrected ? " · rate corrected" : "") + (t.rateExceeded ? " · rate exceeded" : "");
  $("#tsLocal").textContent = t.localMs + " ms"; $("#tsTime").textContent = t.timeMs + " ms";
  $("#tsMaster").textContent = t.masterMs + " ms"; $("#tsOffset").textContent = t.offsetMs + " ms";
  if (changed("ts", t.events)) {
    $("#tsEvents").innerHTML = t.events.length ? t.events.slice().reverse().map(e => `<li><span class="t">${e.t} ms</span>${esc(e.text)}</li>`).join("") : '<li class="muted">no events yet</li>';
  }
}
document.querySelectorAll("[data-ts]").forEach(b => b.onclick = async () => {
  const v = b.dataset.ts;
  if (v === "reset") await post("/tsync/reset");
  else if (v === "adv") await post("/tsync/advance", { ms: 2000 });
  else if (v === "rate") { await post("/tsync/advance", { ms: 200 }); await post("/tsync/sync", { offsetMs: 200, rate: 0.0005 }); }
  else { await post("/tsync/advance", { ms: 200 }); await post("/tsync/sync", { offsetMs: +v }); }
  poll();
});

/* ------------------------------------------------------------ Diagnostics lab */
const UQ = [["Extended session", "10 03"], ["Read VIN", "22 F1 90"], ["Routine (locked)", "31 01 FF 00"], ["Unlock (27 01/02)", "@unlock"],
  ["Routine (unlocked)", "31 01 FF 00"], ["Inject fault", "@fault"], ["ReadDTC", "19 02 FF"], ["Heal fault", "@heal"], ["ClearDTC", "14 FF FF FF"], ["Idle 10 s (S3)", "@idle"]];
$("#udsxQuick").innerHTML = UQ.map(([n, h]) => `<button class="btn" data-h="${h}">${n}</button>`).join("");
$("#udsxQuick").onclick = async e => {
  const h = e.target.dataset.h; if (!h) return;
  if (h === "@unlock") await post("/udsx/unlock");
  else if (h === "@fault") await post("/udsx/fault", { dtc: 0xC10100, active: true });
  else if (h === "@heal") await post("/udsx/fault", { dtc: 0xC10100, active: false });
  else if (h === "@idle") await post("/udsx/advance", { ms: 10000 });
  else await post("/udsx/request", { request: h });
  poll();
};
$("#udsxForm").onsubmit = async e => { e.preventDefault(); const r = await post("/udsx/request", { request: $("#udsxInput").value }); if (!r.ok) alert(r.error); poll(); };
$("#udsxReset").onclick = async () => { await post("/udsx/reset"); sig.ux = ""; poll(); };
function renderUdsx(u) {
  $("#udsxSess").textContent = SESSIONS[u.session] || "?";
  if (!changed("ux", u.trace)) return;
  $("#udsxConsole").innerHTML = u.trace.slice().reverse().map(r => r.kind === "fault"
    ? `<div class="entry"><div class="note">⚡ ${esc(r.text)}</div></div>`
    : `<div class="entry"><div class="req">→ ${esc(r.req)}</div><div class="${r.neg ? "neg" : "pos"}">← ${r.resp === null ? "(none)" : esc(r.resp)}</div><div class="note">${esc(r.respText)}</div></div>`).join("");
}

/* -------------------------------------------------------------- Software update */
function renderUpd(r) {
  if (!r || !changed("upd", r)) return;
  const cls = { "updated": "ok", "rolled-back": "warn", "package-rejected": "bad", "session-rejected": "bad", "error": "bad" }[r.outcome] || "";
  $("#updOutcome").textContent = r.outcome; $("#updOutcome").className = "chip " + cls;
  const bar = $("#updBar"); bar.className = cls; bar.style.width = (r.outcome === "package-rejected" ? 100 : r.outcome === "session-rejected" ? 8 : 100) + "%";
  $("#updStatus").innerHTML = r.statusTrail.map(s => `<li>${esc(s)}</li>`).join("");
  $("#updMachine").innerHTML = r.machineTrail.map(s => `<li>${esc(s)}</li>`).join("");
  $("#updMsg").textContent = `${r.blocks} block(s) of the ${r.size}-byte package transferred · final UCM status ${r.ucmStatus} · machine ${r.machineState}` + (r.message ? " · " + r.message : "");
  $("#updMsg").className = "msg " + (cls === "ok" ? "ok" : cls === "bad" ? "bad" : "");
}
$("#updRun").onclick = async () => {
  const mode = $("#updMode").value, bar = $("#updBar");
  bar.className = ""; bar.style.width = "0";
  $("#updRun").disabled = true; $("#updOutcome").textContent = "transferring…"; $("#updOutcome").className = "chip warn";
  for (let p = 10; p <= 70; p += 20) { bar.style.width = p + "%"; await new Promise(r => setTimeout(r, 120)); }
  await post("/update/run", { size: +$("#updSize").value, [mode || "none"]: true });
  $("#updRun").disabled = false; sig.upd = ""; poll();
};

/* -------------------------------------------------------------------- Key slots */
const SLOT_FOR = { seal: "vault-aes", mac: "vault-hmac", sign: "vault-ed25519" };
function renderKeys(k) {
  if (!k.enabled) { $("#keyTable tbody").innerHTML = '<tr><td colspan="5" class="muted">built without crypto support</td></tr>'; return; }
  if (!changed("keys", k.slots)) return;
  $("#keyTable tbody").innerHTML = k.slots.map(s => `<tr data-slot="${esc(s.name)}"><td>${esc(s.name)}${s.persistent ? "" : ' <span class="muted">(volatile)</span>'}</td><td>${s.algorithm}</td><td>${s.usage}</td>
    <td><span class="pill ${s.state === "occupied" ? "ok" : s.state === "corrupted" ? "bad" : ""}">${s.state}</span></td>
    <td><button data-op="generate">generate</button> <button data-op="tamper">tamper</button> <button data-op="clear">clear</button></td></tr>`).join("");
}
$("#keyTable").onclick = async e => {
  const op = e.target.dataset.op; if (!op) return;
  const r = await post("/keys/" + op, { name: e.target.closest("tr").dataset.slot });
  $("#keyOut").textContent = r.ok ? op + " ok" : "error: " + r.error; $("#keyOut").style.color = r.ok ? "var(--ok)" : "var(--bad)";
  sig.keys = ""; poll();
};
$("#keyReload").onclick = async () => { const r = await post("/keys/reload"); $("#keyOut").textContent = r.ok ? "storage reloaded — persistent keys came back" : "error: " + r.error; sig.keys = ""; poll(); };
$("#keyGo").onclick = async () => {
  const op = $("#keyOp").value, name = SLOT_FOR[op], text = $("#keyText").value, out = [];
  const bad = r => { $("#keyOut").textContent = out.concat("✗ " + (r.error || "failed")).join("\n"); $("#keyOut").style.color = "var(--bad)"; };
  if (op === "seal") {
    const s = await post("/keys/seal", { name, text, aad: "hdr" }); if (!s.ok) return bad(s);
    out.push("sealed: " + s.hex.slice(0, 48) + "…");
    const o = await post("/keys/open", { name, hex: s.hex, aad: "hdr" }); if (!o.ok) return bad(o);
    out.push("opened: \"" + o.text + "\"");
  } else if (op === "mac") {
    const m = await post("/keys/mac", { name, text }); if (!m.ok) return bad(m); out.push("HMAC-SHA256: " + m.hex);
  } else {
    const s = await post("/keys/sign", { name, text }); if (!s.ok) return bad(s);
    out.push("signature: " + s.hex.slice(0, 48) + "…");
    const v = await post("/keys/verify", { name, text, hex: s.hex }); out.push("verify (original): " + v.valid);
    const w = await post("/keys/verify", { name, text: text + "!", hex: s.hex }); out.push("verify (altered message): " + w.valid);
  }
  $("#keyOut").textContent = out.join("\n"); $("#keyOut").style.color = "var(--ok)";
};

/* ------------------------------------------------------------------- Demo apps */
function renderApps(apps) {
  if (!changed("apps", apps)) return;
  $("#appList").innerHTML = apps.map(a => `<div class="app" data-app="${a.name}"><b>${esc(a.title)}</b><p>${esc(a.about)}</p>
    <div class="acts">${a.modes.map(m => `<button class="btn" data-mode="${m}" ${a.available ? "" : "disabled title=\"not built\""}>▶ ${m}</button>`).join("")}</div></div>`).join("");
}
$("#appList").onclick = async e => {
  const mode = e.target.dataset.mode; if (!mode) return;
  const name = e.target.closest(".app").dataset.app, out = $("#appOut");
  document.querySelectorAll("#appList .btn").forEach(b => b.disabled = true);
  out.style.color = ""; out.textContent = `$ ${name} ${mode} …`;
  const r = await post("/apps/run", { name, mode });
  document.querySelectorAll("#appList .btn").forEach(b => b.disabled = false);
  if (!r.ok) { out.textContent = r.error; out.style.color = "var(--bad)"; return; }
  out.textContent = `$ ${name} (${mode}) → exit ${r.exit}${r.timedOut ? " · TIMED OUT" : ""} · ${r.ms} ms\n\n` + r.output + (r.truncated ? "\n… output truncated" : "");
  out.scrollTop = 0;
};
