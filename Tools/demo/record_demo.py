#!/usr/bin/env python3
"""Drive the Adaptive Platform Console and record screenshots, an mp4 and a GIF.

Usage:
  ap_dashboard --port 8099 --data-dir /tmp/ap &
  python3 Tools/demo/record_demo.py --url http://127.0.0.1:8099 --out doc/img [--video] [--gif]
                                    [--steps nm,tsync,...] [--list]

Needs: playwright (pip install playwright) and a Chrome/Chromium binary; ffmpeg for mp4/gif.

The scenario is one function per feature (see STEPS).  Every step waits on real platform state
(never on a fixed sleep for correctness) and *asserts* the behaviour it shows, so the script is also
an end-to-end test: it exits non-zero as soon as the platform misbehaves.  Add a feature -> add a
card, one step function here and a screenshot name (see doc/demo.md).
"""
import argparse, os, shutil, subprocess, sys, time
from playwright.sync_api import sync_playwright

CAPTION_JS = """
(t) => { let c = document.getElementById('demo-caption');
  if (!c) { c = document.createElement('div'); c.id='demo-caption';
    c.style.cssText='position:fixed;left:50%;bottom:18px;transform:translateX(-50%);z-index:99;background:rgba(10,16,34,.92);color:#fff;border:1px solid #4f8cff;border-radius:10px;padding:10px 20px;font:600 16px system-ui;box-shadow:0 8px 30px rgba(0,0,0,.5);max-width:80vw;text-align:center';
    document.body.appendChild(c); }
  c.textContent = t; c.style.display = t ? 'block' : 'none'; }
"""


class Fail(Exception):
    """The platform did not behave as the scenario expects."""


class Demo:
    """Thin helper around a Playwright page: captions, pauses, screenshots, assertions."""

    def __init__(self, page, out, pace):
        self.page, self.out, self.pace = page, out, pace
        page.set_default_timeout(60000)

    def pause(self, s):
        self.page.wait_for_timeout(int(s * 1000 * self.pace))

    def cap(self, text):
        self.page.evaluate(CAPTION_JS, text)

    def focus(self, sel):
        self.page.evaluate("(s) => document.querySelector(s).scrollIntoView({behavior:'smooth', block:'center'})", sel)
        self.pause(0.9)

    def shot(self, name, full=False):
        if full:  # a fixed-position caption would land in the middle of a full-page image
            self.page.evaluate("() => { const c=document.getElementById('demo-caption'); if (c) c.style.visibility='hidden'; }")
        self.page.screenshot(path=os.path.join(self.out, name), full_page=full)
        if full:
            self.page.evaluate("() => { const c=document.getElementById('demo-caption'); if (c) c.style.visibility='visible'; }")

    def card(self, name, sel):
        """Screenshot of one card only (no caption, nothing cut off by the viewport)."""
        self.page.evaluate("() => { const c=document.getElementById('demo-caption'); if (c) c.style.visibility='hidden'; }")
        self.page.locator(sel).screenshot(path=os.path.join(self.out, name))
        self.page.evaluate("() => { const c=document.getElementById('demo-caption'); if (c) c.style.visibility='visible'; }")

    def click(self, sel):
        self.page.click(sel)

    def text(self, sel):
        return self.page.inner_text(sel)

    def wait(self, js, timeout=30000):
        self.page.wait_for_function(js, timeout=timeout)

    def wait_text(self, sel, needle, timeout=30000):
        self.wait(f"document.querySelector({sel!r}).textContent.includes({needle!r})", timeout)

    def check(self, cond, msg):
        if not cond:
            raise Fail(msg)

    def contains(self, sel, needle, msg=None):
        self.check(needle in self.text(sel), msg or f"{sel} does not contain {needle!r}")


# ---------------------------------------------------------------------------- steps
def step_overview(d):
    d.page.wait_for_selector("#w-Brake .status")
    d.cap("Adaptive Platform Console — live view of the running platform"); d.pause(2.5)
    d.shot("01-overview.png", full=True)


def step_exec(d):
    d.focus("#cardEm")
    d.cap("Execution Management starts the platform processes from the manifests")
    d.click("#btnEmStart")
    d.wait("document.querySelector('#emState').textContent === 'running'", 90000)
    d.wait("document.querySelectorAll('#procTable tbody tr').length >= 2")
    d.pause(1.2)
    d.card("02-execution-management.png", "#cardEm")
    d.cap("Stopping the platform: Execution Management terminates every process")
    d.click("#btnEmStop")
    d.wait("document.querySelector('#emState').textContent === 'idle'", 90000)
    d.pause(0.8)


def step_phm(d):
    d.focus("#cardPhm")
    d.cap("Platform Health Management: inject a hang into 'Brake'")
    d.pause(1.2)
    d.click("#w-Brake [data-act=hang]")
    d.wait("document.querySelector('#w-Brake .status').textContent !== 'OK'")
    d.cap("Supervision expires → recovery action restarts the function group")
    d.wait("document.querySelector('#w-Brake .status').textContent === 'EXPIRED'", 15000)
    d.pause(0.8)
    d.wait("document.querySelector('#w-Brake .status').textContent === 'OK' && "
           "Number(document.querySelector('#w-Brake .rs').textContent.split(': ')[1]) >= 1", 30000)
    d.contains("#phmEvents", "EXPIRED", "phm: no EXPIRED event was recorded")
    d.cap("Recovered automatically — restart counter incremented"); d.pause(1.2)
    d.card("03-phm.png", "#cardPhm")
    d.cap("Disable automatic recovery and break 'Telematics' → watchdog escalation")
    d.click("#autoRec + span"); d.pause(0.8)
    d.click("#w-Telematics [data-act=hang]")
    d.page.wait_for_selector("#wdBanner:not(.hidden)", timeout=20000)
    d.pause(1.0)
    d.card("03-phm-watchdog.png", "#cardPhm")
    d.cap("Reset the ECU: platform comes back, boot counter increments")
    boot = int(d.text("#hBoot"))
    d.click("#btnReset")
    d.page.wait_for_selector("#wdBanner.hidden", state="attached", timeout=15000)
    d.wait(f"Number(document.querySelector('#hBoot').textContent) === {boot + 1}")
    d.click("#autoRec + span"); d.pause(0.8)


def step_diag(d):
    d.focus("#cardDiag")
    d.cap("Diagnostics: UDS session control, read and write data identifiers")
    for h in ("22 F1 90", "2E F1 A2 42 6F 62", "10 03", "2E F1 A2 42 6F 62", "22 F1 A2", "22 F1 A0"):
        d.click(f"#cardDiag [data-h='{h}']"); d.pause(0.7)
    d.pause(0.6)
    d.card("04-diagnostics.png", "#cardDiag")
    d.contains("#diagConsole", "Bob", "diagnostics: the written vehicle name was not read back")


def step_per(d):
    d.focus("#cardPer")
    d.cap("Persistency: the written value survives in the CRC-protected key-value store")
    d.page.fill("#perKey", "volume"); d.page.fill("#perVal", "7"); d.click("#perForm .btn")
    d.wait_text("#perTable", "volume")
    d.pause(1.0)
    d.card("05-persistency.png", "#cardPer")


def step_crypto(d):
    d.focus("#cardCrypto")
    d.cap("Cryptography: authenticated encryption detects tampering")
    d.click("#cTabs [data-t=aead]")
    d.click("#cAead [data-op=seal]"); d.pause(0.7)
    d.click("#cAead [data-op=open]"); d.pause(0.9)
    d.contains("#cOut", "decrypted", "crypto: round trip failed")
    d.click("#cTamper"); d.pause(0.9)
    d.contains("#cOut", "rejected", "crypto: tampering was not detected")
    d.card("06-crypto.png", "#cardCrypto")


def nm_state(d, ecu):
    return d.text(f".ecu[data-ecu={ecu}] .st").strip()


def step_nm(d):
    d.focus("#cardNm")
    d.cap("Network Management: three virtual ECUs on a bus, driven by a virtual clock")
    d.pause(1.2)
    d.check(nm_state(d, "Gateway") == "BusSleep", "nm: bus must start asleep")
    d.cap("Gateway requests the network → Repeat Message state, NM PDUs start")
    d.click(".ecu[data-ecu=Gateway] [data-op=request]")
    d.wait("document.querySelector('.ecu[data-ecu=Gateway] .st').textContent === 'RepeatMessage'")
    d.click("[data-nm='200']")
    d.wait("Number(document.querySelector('#nmPdus').textContent.split(' ')[0]) > 0")
    d.pause(0.8)
    d.check(nm_state(d, "Door") == "BusSleep", "nm: Door must ignore PDUs without a partial network it serves")
    d.cap("Partial network 0 requested → only the ECUs serving PN0 wake up (Door), Camera stays asleep")
    d.click(".ecu[data-ecu=Gateway] [data-op=pn0]")
    d.click("[data-nm='200']")
    d.wait("document.querySelector('.ecu[data-ecu=Door] .st').textContent === 'RepeatMessage'")
    d.check(nm_state(d, "Camera") == "BusSleep", "nm: Camera must not wake for PN0")
    d.pause(1.0)
    d.click("[data-nm='1000']")
    d.wait("document.querySelector('.ecu[data-ecu=Gateway] .st').textContent === 'NormalOperation'")
    d.card("07-network-management.png", "#cardNm")
    d.cap("Everyone releases → the bus goes through Ready Sleep and Prepare Bus Sleep back to sleep")
    d.click(".ecu[data-ecu=Gateway] [data-op=release]")
    d.click(".ecu[data-ecu=Gateway] [data-op=pn0]")
    d.pause(0.6)
    d.click("[data-nm='6000']")
    d.wait("['Gateway','Door'].every(n => document.querySelector('.ecu[data-ecu='+n+'] .st').textContent === 'BusSleep')")
    d.contains("#nmEvents", "PrepareBusSleep")
    d.pause(1.2)


def step_tsync(d):
    d.focus("#cardTs")
    d.cap("Time Synchronization: a time base follows a master clock")
    d.pause(1.0)
    d.check("NotSynchronized" in d.text("#tsStatus"), "tsync: must start unsynchronized")
    d.click("[data-ts='0']")
    d.wait_text("#tsStatus", "Synchronized")
    d.pause(0.8)
    d.cap("Small offset (+20 ms) is slewed smoothly — no time leap")
    d.click("[data-ts='20']"); d.pause(1.2)
    d.contains("#tsFlags", "leap: none", "tsync: small offset must not cause a leap")
    d.cap("Large offset (+2 s) steps the clock and reports a future time leap")
    d.click("[data-ts='2000']")
    d.wait_text("#tsFlags", "leap: future")
    d.card("08-time-sync.png", "#cardTs")
    d.cap("A rate deviation of 500 ppm is detected and corrected")
    d.click("[data-ts='rate']")
    d.wait_text("#tsFlags", "rate")
    d.pause(1.0)
    d.cap("No update from the master for 2 s → the time base times out")
    d.click("[data-ts='adv']")
    d.wait_text("#tsStatus", "TimeOut")
    d.pause(1.4)


def step_udsx(d):
    d.focus("#cardUdsx")
    d.cap("Diagnostics lab: a demo ECU with security access, routines and a DTC memory")
    q = lambda label: d.click(f"#udsxQuick >> text={label}")
    q("Extended session"); d.pause(0.7)
    q("Routine (locked)"); d.pause(0.7)
    d.contains("#udsxConsole", "negative response", "udsx: routine must be refused while locked")
    d.cap("Security access: seed/key exchange unlocks level 1")
    q("Unlock"); d.pause(0.9)
    q("Routine (unlocked)"); d.pause(0.9)
    d.cap("Inject a fault → the DTC appears in the fault memory (ReadDTC 19 02)")
    q("Inject fault"); d.pause(0.5)
    q("ReadDTC"); d.pause(0.9)
    d.contains("#udsxConsole", "C1 01 00", "udsx: injected DTC not reported")
    d.card("09-diagnostics-lab.png", "#cardUdsx")
    d.cap("ClearDTC empties the memory again")
    q("Heal fault"); q("ClearDTC"); d.pause(0.6)
    q("ReadDTC"); d.pause(1.2)
    first = d.page.inner_text("#udsxConsole .entry:first-child")
    d.check("C1 01 00" not in first, "udsx: DTC memory not cleared")
    q("Idle 10 s"); d.wait_text("#udsxSess", "default"); d.pause(0.6)


def step_update(d):
    d.focus("#cardUpd")
    d.cap("Software Update: a package is transferred in blocks, verified and activated")
    d.page.select_option("#updSize", "100000")
    d.click("#updRun")
    d.wait_text("#updOutcome", "updated"); d.pause(1.0)
    d.contains("#updStatus", "kActivated", "update: UCM did not reach kActivated")
    d.card("10-software-update.png", "#cardUpd")
    d.cap("A package tampered with in transit is rejected by the digest check")
    d.page.select_option("#updMode", "tamper"); d.click("#updRun")
    d.wait_text("#updOutcome", "package-rejected"); d.pause(1.2)
    d.cap("The new software fails verification → automatic rollback")
    d.page.select_option("#updMode", "failVerify"); d.click("#updRun")
    d.wait_text("#updOutcome", "rolled-back"); d.pause(1.4)


def step_keys(d):
    d.focus("#cardKeys")
    d.cap("Key slots: keys are generated inside the key store and persisted wrapped")
    d.click("tr[data-slot=vault-aes] [data-op=generate]")
    d.wait("document.querySelector('tr[data-slot=vault-aes] .pill').textContent === 'occupied'")
    d.click("#keyGo"); d.wait_text("#keyOut", "opened:"); d.pause(0.9)
    d.cap("Sign and verify with a stored Ed25519 key; an altered message fails")
    d.click("tr[data-slot=vault-ed25519] [data-op=generate]")
    d.wait("document.querySelector('tr[data-slot=vault-ed25519] .pill').textContent === 'occupied'")
    d.page.select_option("#keyOp", "sign"); d.click("#keyGo")
    d.wait_text("#keyOut", "altered message): false"); d.pause(0.9)
    d.page.select_option("#keyOp", "seal")
    d.card("11-key-slots.png", "#cardKeys")
    d.cap("Someone alters the stored key blob → the slot is detected as corrupted")
    d.click("tr[data-slot=vault-aes] [data-op=tamper]")
    d.wait("document.querySelector('tr[data-slot=vault-aes] .pill').textContent === 'corrupted'")
    d.click("#keyGo"); d.pause(1.0)
    d.contains("#keyOut", "✗", "keys: a corrupted slot must refuse to work")
    d.cap("Regenerating repairs the slot; the other keys survive a reload from storage")
    d.click("tr[data-slot=vault-aes] [data-op=generate]")
    d.wait("document.querySelector('tr[data-slot=vault-aes] .pill').textContent === 'occupied'")
    d.click("#keyReload"); d.wait_text("#keyOut", "came back"); d.pause(1.2)


def run_app(d, app, mode, expect_exit, needle):
    d.click(f".app[data-app={app}] [data-mode={mode}]")
    d.wait_text("#appOut", f"exit {expect_exit}", 60000)
    d.contains("#appOut", needle, f"app {app} {mode}: expected output {needle!r}")


def step_apps(d):
    d.focus("#cardApps")
    d.cap("Demo applications: run the real binaries from the console, output captured")
    d.pause(0.8)
    run_app(d, "uds_tester", "run", 0, "passed"); d.pause(1.2)
    d.card("12-demo-apps.png", "#cardApps")
    d.cap("The OTA updater with a tampered package exits with the 'package rejected' code")
    run_app(d, "ota_updater", "tamper", 3, "rejected"); d.pause(1.2)
    d.cap("The NM bus monitor and the time-sync clock run as stand-alone apps too")
    run_app(d, "nm_bus_monitor", "run", 0, "Gateway"); d.pause(0.9)
    run_app(d, "time_sync_clock", "run", 0, "ync"); d.pause(0.9)


def step_final(d):
    d.page.evaluate("() => window.scrollTo({top: 0, behavior: 'smooth'})"); d.pause(1.0)
    d.cap("Every card above ran against the real ara:: clusters"); d.pause(2.2)
    d.shot("13-final.png", full=True)
    d.cap("")


STEPS = [("overview", step_overview), ("exec", step_exec), ("phm", step_phm), ("diag", step_diag), ("per", step_per),
         ("crypto", step_crypto), ("nm", step_nm), ("tsync", step_tsync), ("udsx", step_udsx), ("update", step_update),
         ("keys", step_keys), ("apps", step_apps), ("final", step_final)]


# ---------------------------------------------------------------------------- media
def make_media(a, vdir):
    vids = [f for f in os.listdir(vdir) if f.endswith(".webm")]
    ffmpeg = shutil.which("ffmpeg")
    if not vids or not ffmpeg:
        raise Fail("no recorded video or no ffmpeg available")
    src = os.path.join(vdir, vids[0])
    mp4 = os.path.join(a.out, "platform-demo.mp4")
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", src, "-c:v", "libx264", "-pix_fmt", "yuv420p",
                    "-preset", "veryfast", "-crf", "26", "-movflags", "+faststart", mp4], check=True)
    print("video:", mp4)
    if a.gif:
        gif = os.path.join(a.out, "platform-demo.gif")
        for fps, width, colors in ((8, 900, 32), (8, 840, 32), (7, 800, 32), (6, 720, 48)):   # first one that fits the size budget
            vf = f"fps={fps},scale={width}:-1:flags=lanczos,split[s0][s1];[s0]palettegen=max_colors={colors}:stats_mode=diff[p];[s1][p]paletteuse=dither=none:diff_mode=rectangle"
            subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", mp4, "-filter_complex", vf, "-loop", "0", gif], check=True)
            size = os.path.getsize(gif)
            print(f"gif: {gif} {size / 1e6:.1f} MB ({fps} fps, {width}px)")
            if size <= 8 * 1024 * 1024:
                break
        else:
            raise Fail("gif larger than 8 MB even at the lowest quality")
    shutil.rmtree(vdir, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8099")
    ap.add_argument("--out", default="doc/img")
    ap.add_argument("--video", action="store_true", help="record doc/img/platform-demo.mp4")
    ap.add_argument("--gif", action="store_true", help="also derive platform-demo.gif (implies --video)")
    ap.add_argument("--steps", default="", help="comma separated subset of steps (default: all)")
    ap.add_argument("--list", action="store_true", help="list the step names and exit")
    ap.add_argument("--pace", type=float, default=1.0, help="multiplier for the pauses between steps (video)")
    a = ap.parse_args()
    if a.list:
        print("\n".join(n for n, _ in STEPS))
        return 0
    a.video = a.video or a.gif
    wanted = [s for s in a.steps.split(",") if s]
    unknown = set(wanted) - {n for n, _ in STEPS}
    if unknown:
        print("unknown step(s):", ", ".join(sorted(unknown)), file=sys.stderr)
        return 2
    os.makedirs(a.out, exist_ok=True)
    chrome = next((p for p in ("/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser") if os.path.exists(p)), None)
    vdir = os.path.join(a.out, "_video")
    t0 = time.time()
    try:
        with sync_playwright() as pw:
            browser = pw.chromium.launch(executable_path=chrome, args=["--no-sandbox"])
            ctx = browser.new_context(viewport={"width": 1440, "height": 900},
                                      record_video_dir=vdir if a.video else None,
                                      record_video_size={"width": 1440, "height": 900})
            d = Demo(ctx.new_page(), a.out, a.pace)
            d.page.goto(a.url)
            d.page.wait_for_selector("#w-Brake .status")
            for name, fn in STEPS:
                if wanted and name not in wanted:
                    continue
                s0 = time.time()
                try:
                    fn(d)
                except Exception as e:     # attribute the failure to its step, keep a screenshot for the log
                    try:
                        d.page.screenshot(path=os.path.join(a.out, f"_failed-{name}.png"))
                    except Exception:
                        pass
                    raise Fail(f"step '{name}' failed: {type(e).__name__}: {str(e).splitlines()[0] if str(e) else ''}")
                print(f"step {name:9s} ok  {time.time() - s0:5.1f}s", flush=True)
            ctx.close(); browser.close()
        if a.video:
            make_media(a, vdir)
    except Fail as e:
        print("DEMO SCENARIO FAILED:", e, file=sys.stderr)
        return 1
    print(f"demo scenario OK ({time.time() - t0:.0f}s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
