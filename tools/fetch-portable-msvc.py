#!/usr/bin/env python3
"""
Portable MSVC + Windows SDK fetcher for LumaForge.

Downloads the *official* Microsoft packages that the Visual Studio installer
would use (from the public VS release manifest), verifies SHA-256, and extracts
them into plain directories. Nothing is registered with Windows and nothing is
installed on C:.

  MSVC toolset   -> <msvc-dest>\\<version>\\{bin,include,lib,...}
                    <msvc-dest>\\Redist\\...            (runtime DLLs for deployment / debug runs)
  Windows SDK    -> <sdk-dest>\\{Include,Lib,bin,...}\\<sdk-version>

Usage:
  python fetch-portable-msvc.py --msvc-dest G:\\DevTools\\MSVC --sdk-dest G:\\DevTools\\WindowsSDK --cache G:\\Caches\\msvc
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile
from urllib.parse import unquote

CHANNEL_URL = "https://aka.ms/vs/17/release/channel"
HOST, TARGET = "X64", "X64"

# The minimum Windows SDK subset for building a native x64 C++ desktop app
# (headers, x64 libs, x64 tools such as rc/mt/fxc, UCRT, signing tools).
SDK_MSIS = [
    "Windows SDK Desktop Headers x64-x86_en-us.msi",
    "Windows SDK Desktop Headers x86-x86_en-us.msi",
    "Windows SDK Desktop Libs x64-x86_en-us.msi",
    "Windows SDK Desktop Tools x64-x86_en-us.msi",
    "Windows SDK OnecoreUap Headers x64-x86_en-us.msi",
    "Windows SDK OnecoreUap Headers x86-x86_en-us.msi",
    "Windows SDK for Windows Store Apps Headers-x86_en-us.msi",
    "Windows SDK for Windows Store Apps Headers OnecoreUap-x86_en-us.msi",
    "Windows SDK for Windows Store Apps Libs-x86_en-us.msi",
    "Windows SDK for Windows Store Apps Tools-x86_en-us.msi",
    "Windows SDK Signing Tools-x86_en-us.msi",
    "Universal CRT Headers Libraries and Sources-x86_en-us.msi",
]
# Debugging Tools for Windows (cdb etc.) are NOT part of the VS manifest; see
# tools/fetch-debuggers.ps1 for the optional winsdksetup.exe layout route.


def log(msg):
    print(msg, flush=True)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest, expected_sha=None, size=None):
    if os.path.exists(dest) and (expected_sha is None or sha256(dest).lower() == expected_sha.lower()):
        return dest
    tmp = dest + ".part"
    log("  downloading %s%s" % (os.path.basename(dest), " (%.1f MB)" % (size / 1e6) if size else ""))
    req = urllib.request.Request(url, headers={"User-Agent": "LumaForge-toolchain-setup"})
    with urllib.request.urlopen(req) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    if expected_sha and sha256(tmp).lower() != expected_sha.lower():
        os.remove(tmp)
        raise RuntimeError("SHA-256 mismatch for %s" % dest)
    os.replace(tmp, dest)
    return dest


def fetch_json(url, dest):
    download(url, dest)
    with open(dest, encoding="utf-8") as f:
        return json.load(f)


def first(pkgs, pid, chip=None):
    for p in pkgs:
        if p["id"] == pid and p.get("language") in (None, "en-US", "neutral") and (chip is None or p.get("chip") == chip):
            return p
    raise KeyError("package not found in manifest: %s" % pid)


def extract_vsix(path, dest):
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            name = unquote(info.filename)
            if not name.startswith("Contents/") or info.is_dir():
                continue
            out = os.path.join(dest, *name[len("Contents/"):].split("/"))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(info) as src, open(out, "wb") as dst:
                shutil.copyfileobj(src, dst)


def msi_cabs(msi_path):
    data = open(msi_path, "rb").read()
    i = 0
    while True:
        i = data.find(b".cab", i + 4)
        if i < 0:
            return
        yield data[i - 32:i + 4].decode("ascii", "ignore")


def msi_admin_extract(msi_path, target_dir):
    # Administrative install == pure extraction. Nothing is registered and
    # nothing is cached under C:\Windows\Installer.
    log_path = os.path.join(os.environ.get("TEMP", tempfile.gettempdir()), "lf-msiexec.log")
    cmd = ["msiexec.exe", "/a", msi_path, "/qn", "/norestart", "TARGETDIR=%s" % target_dir, "/L*", log_path]
    r = subprocess.run(cmd)
    if r.returncode != 0:
        raise RuntimeError("msiexec failed (%d) for %s; see %s" % (r.returncode, os.path.basename(msi_path), log_path))


def move_tree_up(src_root, dest_root):
    """Merge src_root/* into dest_root, then remove src_root."""
    os.makedirs(dest_root, exist_ok=True)
    for item in os.listdir(src_root):
        s, d = os.path.join(src_root, item), os.path.join(dest_root, item)
        if os.path.isdir(s) and os.path.isdir(d):
            move_tree_up(s, d)
        else:
            if os.path.exists(d):
                if os.path.isdir(d):
                    shutil.rmtree(d)
                else:
                    os.remove(d)
            shutil.move(s, d)
    shutil.rmtree(src_root, ignore_errors=True)



def extract_msvc(a, pkgs, ver, ids):
    """Downloads and extracts the MSVC toolset + redist + debug UCRT. Returns the toolset version."""
    log("== MSVC toolset")
    stage = os.path.join(a.cache, "msvc_stage")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    for pid in ids:
        p = first(pkgs, pid)
        for pl in p["payloads"]:
            f = download(pl["url"], os.path.join(a.cache, os.path.basename(pl["fileName"])), pl.get("sha256"), pl.get("size"))
            extract_vsix(f, stage)

    tools_root = os.path.join(stage, "VC", "Tools", "MSVC")
    tool_ver = os.listdir(tools_root)[0]
    dest_tools = os.path.join(a.msvc_dest, tool_ver)
    if os.path.isdir(dest_tools):
        shutil.rmtree(dest_tools)
    shutil.move(os.path.join(tools_root, tool_ver), dest_tools)

    redist_root = os.path.join(stage, "VC", "Redist", "MSVC")
    if os.path.isdir(redist_root):
        for rv in os.listdir(redist_root):
            d = os.path.join(a.msvc_dest, "Redist", rv)
            if os.path.isdir(d):
                shutil.rmtree(d)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.move(os.path.join(redist_root, rv), d)
    aux = os.path.join(stage, "VC", "Auxiliary")
    if os.path.isdir(aux):
        d = os.path.join(a.msvc_dest, "Auxiliary")
        shutil.rmtree(d, ignore_errors=True)
        shutil.move(aux, d)
    leftovers = [os.path.relpath(os.path.join(root, fn), stage) for root, _, files in os.walk(stage) for fn in files]
    if leftovers:
        log("   note: %d unclaimed staged file(s), e.g. %s" % (len(leftovers), leftovers[:5]))
    shutil.rmtree(stage, ignore_errors=True)

    # remove the compiler telemetry helper
    for root, _, files in os.walk(dest_tools):
        for fn in files:
            if fn.lower() == "vctip.exe":
                os.remove(os.path.join(root, fn))
    log("   MSVC %s -> %s" % (tool_ver, dest_tools))

    # debug UCRT (ucrtbased.dll and friends) ships as an MSI
    log("== Debug UCRT runtime")
    dbg = first(pkgs, "Microsoft.VisualCpp.RuntimeDebug.14", chip="x64")
    dbg_dir = os.path.join(a.cache, "runtimedebug_x64")
    os.makedirs(dbg_dir, exist_ok=True)
    for pl in dbg["payloads"]:
        download(pl["url"], os.path.join(dbg_dir, pl["fileName"]), pl.get("sha256"), pl.get("size"))
    dbg_stage = os.path.join(a.cache, "runtimedebug_stage")
    shutil.rmtree(dbg_stage, ignore_errors=True)
    msi_admin_extract(os.path.join(dbg_dir, "vc_RuntimeDebug.msi"), dbg_stage)
    dbg_out = os.path.join(a.msvc_dest, "Redist", "debug_ucrt_x64")
    os.makedirs(dbg_out, exist_ok=True)
    for root, _, files in os.walk(dbg_stage):
        for fn in files:
            if fn.lower().endswith(".dll"):
                shutil.copy2(os.path.join(root, fn), dbg_out)
    shutil.rmtree(dbg_stage, ignore_errors=True)
    log("   -> %s: %s" % (dbg_out, os.listdir(dbg_out)))
    return tool_ver


def extract_sdk(a, pkgs):
    """Downloads and extracts the Windows SDK subset in SDK_MSIS. Returns the SDK version."""
    log("== Windows SDK")
    sdk = sorted([p for p in pkgs if re.match(r"Win1[01]SDK_10\.0\.\d+$", p["id"])], key=lambda p: int(p["id"].split(".")[-1]))[-1]
    log("   package %s" % sdk["id"])
    by_name = {pl["fileName"].split("\\")[-1]: pl for pl in sdk["payloads"]}
    sdk_cache = os.path.join(a.cache, "sdk")
    os.makedirs(sdk_cache, exist_ok=True)
    msis = []
    for name in SDK_MSIS:
        pl = by_name[name]
        msis.append(download(pl["url"], os.path.join(sdk_cache, name), pl.get("sha256"), pl.get("size")))
    cabs = set()
    for m in msis:
        cabs.update(msi_cabs(m))
    for c in sorted(cabs):
        pl = by_name[c]
        download(pl["url"], os.path.join(sdk_cache, c), pl.get("sha256"), pl.get("size"))
    sdk_stage = os.path.join(a.cache, "sdk_stage")
    shutil.rmtree(sdk_stage, ignore_errors=True)
    for m in msis:
        log("  extracting %s" % os.path.basename(m))
        msi_admin_extract(m, sdk_stage)
    kits = os.path.join(sdk_stage, "Windows Kits", "10")
    if not os.path.isdir(kits):
        raise RuntimeError("unexpected SDK layout under %s: %s" % (sdk_stage, os.listdir(sdk_stage)))
    move_tree_up(kits, a.sdk_dest)
    shutil.rmtree(sdk_stage, ignore_errors=True)
    sdk_ver = sorted(os.listdir(os.path.join(a.sdk_dest, "Include")))[-1]
    log("   Windows SDK %s -> %s" % (sdk_ver, a.sdk_dest))
    return sdk_ver


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--msvc-dest", required=True)
    ap.add_argument("--sdk-dest", required=True)
    ap.add_argument("--cache", required=True)
    ap.add_argument("--no-asan", action="store_true", help="skip AddressSanitizer runtime (64 MB)")
    ap.add_argument("--skip-msvc", action="store_true", help="leave the installed MSVC toolset alone; only (re)extract the SDK")
    ap.add_argument("--skip-sdk", action="store_true", help="leave the installed Windows SDK alone; only (re)extract MSVC")
    a = ap.parse_args()
    for d in (a.msvc_dest, a.sdk_dest, a.cache):
        os.makedirs(d, exist_ok=True)

    log("== Reading Visual Studio release manifest")
    channel = fetch_json(CHANNEL_URL, os.path.join(a.cache, "channel.json"))
    man_item = next(i for i in channel["channelItems"] if i["id"] == "Microsoft.VisualStudio.Manifests.VisualStudio")
    manifest = fetch_json(man_item["payloads"][0]["url"], os.path.join(a.cache, "manifest.json"))
    pkgs = manifest["packages"]
    log("   VS channel %s" % channel.get("info", {}).get("productDisplayVersion", "?"))

    pat = re.compile(r"Microsoft\.VC\.(\d+\.\d+\.\d+\.\d+)\.Tools\.Host%s\.Target%s\.base$" % (HOST, TARGET))
    versions = sorted({m.group(1) for p in pkgs for m in [pat.match(p["id"])] if m}, key=lambda v: tuple(map(int, v.split("."))))
    ver = versions[-1]
    log("   MSVC toolset package version %s" % ver)

    ids = [
        "Microsoft.VC.%s.Tools.Host%s.Target%s.base" % (ver, HOST, TARGET),
        "Microsoft.VC.%s.Tools.Host%s.Target%s.Res.base" % (ver, HOST, TARGET),
        "Microsoft.VC.%s.CRT.Headers.base" % ver,
        "Microsoft.VC.%s.CRT.%s.Desktop.base" % (ver, TARGET.lower()),
        "Microsoft.VC.%s.CRT.%s.Store.base" % (ver, TARGET.lower()),
        "Microsoft.VC.%s.CRT.Redist.%s.base" % (ver, TARGET),
        "Microsoft.VC.%s.CRT.Source.base" % ver,
    ]
    if not a.no_asan:
        ids += ["Microsoft.VC.%s.ASAN.Headers.base" % ver, "Microsoft.VC.%s.ASAN.%s.base" % (ver, TARGET)]

    if a.skip_msvc:
        existing = [d for d in os.listdir(a.msvc_dest) if re.match(r"\d+\.\d+\.\d+$", d)]
        tool_ver = sorted(existing, key=lambda v: tuple(map(int, v.split("."))))[-1] if existing else "?"
        log("== MSVC toolset: skipped (keeping %s)" % tool_ver)
    else:
        tool_ver = extract_msvc(a, pkgs, ver, ids)

    if a.skip_sdk:
        inc = os.path.join(a.sdk_dest, "Include")
        sdk_ver = sorted(os.listdir(inc))[-1] if os.path.isdir(inc) and os.listdir(inc) else "?"
        log("== Windows SDK: skipped (keeping %s)" % sdk_ver)
    else:
        sdk_ver = extract_sdk(a, pkgs)

    with open(os.path.join(a.msvc_dest, "PORTABLE-TOOLCHAIN.json"), "w") as f:
        json.dump({"msvc_version": tool_ver, "msvc_manifest_version": ver, "sdk_version": sdk_ver,
                   "vs_channel": channel.get("info", {}).get("productDisplayVersion"),
                   "sdk_msis": SDK_MSIS}, f, indent=2)
    log("== Done")


if __name__ == "__main__":
    main()
