#!/usr/bin/env python3
"""Drive the Adaptive Platform Console and record screenshots + a demo video.

Usage:
  ap_dashboard --port 8099 &
  python3 Tools/demo/record_demo.py --url http://127.0.0.1:8099 --out docs/img [--video]

Needs: playwright (pip install playwright) and a Chrome/Chromium binary; ffmpeg for the mp4.
Every step waits on real platform state (no fixed sleeps for correctness), so the script
doubles as an end-to-end test: it exits non-zero if the platform does not behave as expected.
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

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8099")
    ap.add_argument("--out", default="docs/img")
    ap.add_argument("--video", action="store_true")
    ap.add_argument("--pace", type=float, default=1.0, help="multiplier for the pauses between steps (video)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    chrome = next((p for p in ("/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser") if os.path.exists(p)), None)
    vdir = os.path.join(a.out, "_video")
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=chrome, args=["--no-sandbox"])
        ctx = browser.new_context(viewport={"width": 1440, "height": 900},
                                  record_video_dir=vdir if a.video else None,
                                  record_video_size={"width": 1440, "height": 900})
        page = ctx.new_page()
        page.set_default_timeout(15000)
        pause = lambda s: page.wait_for_timeout(int(s * 1000 * a.pace))
        cap = lambda t: page.evaluate(CAPTION_JS, t)
        def shot(name, full=False):
            if full:  # a fixed-position caption would land in the middle of a full-page image
                page.evaluate("() => { const c=document.getElementById('demo-caption'); if (c) c.style.visibility='hidden'; }")
            page.screenshot(path=os.path.join(a.out, name), full_page=full)
            if full:
                page.evaluate("() => { const c=document.getElementById('demo-caption'); if (c) c.style.visibility='visible'; }")
        focus = lambda sel: (page.evaluate("(s) => document.querySelector(s).scrollIntoView({behavior:'smooth', block:'center'})", sel), pause(0.9))

        page.goto(a.url)
        page.wait_for_selector("#w-Brake .status")
        cap("Adaptive Platform Console — live view of the running platform"); pause(2.5)
        shot("01-overview.png", full=True)

        # --- Execution Management drives the real legacy processes ----------------
        focus("#cardEm")
        cap("Execution Management starts the platform processes from the manifests")
        page.click("#btnEmStart")
        page.wait_for_function("document.querySelectorAll('#procTable tbody tr').length >= 2")
        pause(1.2)
        shot("02-execution-management.png")
        page.wait_for_function("document.querySelector('#emState').textContent === 'idle'", timeout=40000)
        pause(1.0)
        shot("03-execution-management-done.png", full=True)

        # --- PHM: fault -> FAILED/EXPIRED -> recovery restarts the function group --
        focus("#cardPhm")
        cap("Platform Health Management: inject a hang into 'Brake'")
        pause(1.5)
        page.click("#w-Brake [data-act=hang]")
        page.wait_for_function("document.querySelector('#w-Brake .status').textContent !== 'OK'")
        shot("04-phm-fault-detected.png")
        cap("Supervision expires → recovery action restarts the function group")
        page.wait_for_function("document.querySelector('#w-Brake .status').textContent === 'EXPIRED'", timeout=10000)
        pause(1.2)
        shot("05-phm-expired.png")
        page.wait_for_function("document.querySelector('#w-Brake .status').textContent === 'OK' && Number(document.querySelector('#w-Brake .rs').textContent.split(': ')[1]) >= 1", timeout=20000)
        events = page.inner_text("#phmEvents")
        if "EXPIRED" not in events:
            sys.exit("phm: no EXPIRED event was recorded")
        cap("Recovered automatically — restart counter incremented"); pause(1.8)
        shot("05-phm-recovered.png")

        cap("Disable automatic recovery and break 'Telematics' → watchdog escalation")
        page.click("#autoRec + span"); pause(1.0)
        page.click("#w-Telematics [data-act=hang]")
        page.wait_for_selector("#wdBanner:not(.hidden)", timeout=15000)
        pause(1.5)
        shot("06-phm-watchdog.png")
        cap("Reset the ECU: platform comes back, boot counter increments"); pause(1.5)
        boot_before = int(page.inner_text("#hBoot"))
        page.click("#btnReset")
        page.wait_for_selector("#wdBanner.hidden", state="attached", timeout=10000)
        page.wait_for_function(f"Number(document.querySelector('#hBoot').textContent) === {boot_before + 1}")
        page.click("#autoRec + span"); pause(1.0)

        # --- Diagnostics -----------------------------------------------------------
        focus("#cardDiag")
        cap("Diagnostics: UDS session control, read and write data identifiers")
        page.click("#cardDiag [data-h='22 F1 90']"); pause(0.8)
        page.click("#cardDiag [data-h='2E F1 A2 42 6F 62']"); pause(0.8)      # rejected in default session
        page.click("#cardDiag [data-h='10 03']"); pause(0.8)
        page.click("#cardDiag [data-h='2E F1 A2 42 6F 62']"); pause(0.8)      # accepted in extended session
        page.click("#cardDiag [data-h='22 F1 A2']"); pause(0.8)
        page.click("#cardDiag [data-h='22 F1 A0']"); pause(1.2)
        shot("07-diagnostics.png")
        if "Bob" not in page.inner_text("#diagConsole"):
            sys.exit("diagnostics: the written vehicle name was not read back")

        # --- Persistency -----------------------------------------------------------
        focus("#cardPer")
        cap("Persistency: the written value survives in the CRC-protected key-value store")
        page.fill("#perKey", "volume"); page.fill("#perVal", "7"); page.click("#perForm .btn"); pause(1.2)
        shot("08-persistency.png")

        # --- Crypto ----------------------------------------------------------------
        focus("#cardCrypto")
        cap("Cryptography: authenticated encryption detects tampering")
        page.click("#cTabs [data-t=aead]")
        page.click("#cAead [data-op=seal]"); pause(0.8)
        page.click("#cAead [data-op=open]"); pause(1.0)
        if "decrypted" not in page.inner_text("#cOut"):
            sys.exit("crypto: round trip failed")
        page.click("#cTamper"); pause(1.0)
        if "rejected" not in page.inner_text("#cOut"):
            sys.exit("crypto: tampering was not detected")
        shot("09-crypto.png")
        page.evaluate("() => window.scrollTo({top: 0, behavior: 'smooth'})"); pause(1.0)
        cap("Everything above ran against the real ara:: clusters"); pause(2.0)
        shot("10-final.png", full=True)
        cap("")
        ctx.close(); browser.close()

    if a.video:
        vids = [f for f in os.listdir(vdir) if f.endswith(".webm")]
        if vids and shutil.which("ffmpeg"):
            src = os.path.join(vdir, vids[0])
            dst = os.path.join(a.out, "platform-demo.mp4")
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", src, "-c:v", "libx264", "-pix_fmt", "yuv420p",
                            "-preset", "veryfast", "-crf", "26", "-movflags", "+faststart", dst], check=True)
            print("video:", dst)
        shutil.rmtree(vdir, ignore_errors=True)
    print("demo scenario OK")

if __name__ == "__main__":
    main()
