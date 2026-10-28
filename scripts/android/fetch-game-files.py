#!/usr/bin/env python3
"""Fetches the two game files the Android build needs, for the cloud build.

  fetch-game-files.py <url> <game folder> <download folder>

<url> is a direct download link (Google Drive and Dropbox share links work)
to either your Xbox 360 ISO of the game, or a .zip, .rar or .7z that holds
default.xex and xeshader.sdb (anywhere inside; .rar and .7z need 7-Zip, which
GitHub's Windows machines have). Only those two files are written to
<game folder>; the download is deleted afterwards. Nothing about the files
but their names, sizes and the executable's revision check is printed, so the
public build log shows nothing of the game.

The ISO is read in place (XDVDFS): only the two files are copied out of it,
instead of extracting all 7 GB.
"""
from __future__ import annotations

import hashlib
import re
import shutil
import struct
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

WANTED = ("default.xex", "xeshader.sdb")
XEX_SHA256 = "8F70E79443E36B38E44B6A105DAD51FB8909FB615D982753CABAAC9B15F0D576"
SECTOR = 2048
XDVDFS_MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Where the game partition starts: Xbox 360 discs (XGD2, XGD3), original Xbox
# discs (XGD1), and images that only hold the game partition (extract-xiso).
PARTITION_OFFSETS = (0xFD90000, 0x2080000, 0x18300000, 0)


def hide_link(text: str, url: str) -> str:
    """Keeps the private link (and a Drive file id) out of the public build log."""
    text = text.replace(url, "<link>")
    match = re.search(r"/d/([\w-]{10,})|[?&]id=([\w-]{10,})", url)
    if match:
        text = text.replace(match.group(1) or match.group(2), "<id>")
    return text


def fetch(url: str, target: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(request, timeout=120) as response, open(target, "wb") as out:
        shutil.copyfileobj(response, out, length=16 << 20)


def is_web_page(path: Path) -> bool:
    with open(path, "rb") as f:
        return f.read(512).lstrip().lower().startswith((b"<!doctype", b"<html"))


def download(url: str, target: Path) -> None:
    drive_id = None
    if "drive.google.com" in url or "docs.google.com" in url:
        match = re.search(r"/d/([\w-]{10,})|[?&]id=([\w-]{10,})", url)
        if not match:
            raise SystemExit("This Google Drive link has no file id: use the file's share link")
        drive_id = match.group(1) or match.group(2)
    if drive_id:
        # Drive's direct download; confirm=t skips the "too large to scan" page.
        fetch(f"https://drive.usercontent.google.com/download?id={drive_id}&export=download&confirm=t", target)
        if not is_web_page(target):
            return
        # Drive answered with a page (a quota or sign-in page): try gdown, which
        # knows Drive's other confirmation steps.
        print("Drive answered with a page; retrying with gdown", flush=True)
        subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", "gdown"], check=True)
        import gdown  # noqa: PLC0415

        try:
            ok = gdown.download(id=drive_id, output=str(target), quiet=True)
        except Exception as error:  # gdown raises on files it cannot reach
            raise SystemExit(f"Google Drive download failed: {hide_link(str(error), url)}\n"
                             "Is the file shared as 'Anyone with the link'?") from None
        if not ok:
            raise SystemExit("Google Drive download failed: is the file shared as 'Anyone with the link'?")
        return
    if "dropbox.com" in url:
        url = re.sub(r"([?&])dl=0", r"\1dl=1", url)
        if "dl=1" not in url:
            url += ("&" if "?" in url else "?") + "dl=1"
    fetch(url, target)


def from_zip(path: Path, game: Path) -> set[str]:
    found = set()
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            name = Path(info.filename).name.lower()
            if name in WANTED and name not in found:
                with archive.open(info) as src, open(game / name, "wb") as out:
                    shutil.copyfileobj(src, out, length=16 << 20)
                found.add(name)
    return found


def find_7zip() -> str:
    for candidate in (shutil.which("7z"), shutil.which("7za"), r"C:\Program Files\7-Zip\7z.exe"):
        if candidate and Path(candidate).exists():
            return candidate
    raise SystemExit("Extracting a .rar or .7z needs 7-Zip")


def from_7zip_archive(path: Path, game: Path) -> set[str]:
    # e: without folders; -r: the names match in any folder of the archive.
    subprocess.run([find_7zip(), "e", "-y", "-r", f"-o{game}", str(path), *WANTED],
                   check=True, stdout=subprocess.DEVNULL)
    return {name for name in WANTED if (game / name).is_file()}


def from_iso(path: Path, game: Path) -> set[str]:
    with open(path, "rb") as iso:
        for partition in PARTITION_OFFSETS:
            iso.seek(partition + 32 * SECTOR)
            header = iso.read(28)
            if header[:20] == XDVDFS_MAGIC:
                break
        else:
            raise SystemExit("Not an Xbox 360 disc image (no XDVDFS volume found)")
        root_sector, root_size = struct.unpack_from("<II", header, 20)
        iso.seek(partition + root_sector * SECTOR)
        table = iso.read(root_size)
        # The root directory is a binary tree of entries: left and right
        # offsets (in 4-byte units), first sector, size, attributes, name.
        entries: dict[str, tuple[int, int]] = {}
        pending, seen = [0], set()
        while pending:
            offset = pending.pop()
            if offset in seen or offset + 14 > len(table):
                continue
            seen.add(offset)
            left, right, sector, size, _attributes, name_length = struct.unpack_from("<HHIIBB", table, offset)
            if left == 0xFFFF and right == 0xFFFF and sector == 0xFFFFFFFF:
                continue  # Padding.
            name = table[offset + 14:offset + 14 + name_length].decode("latin-1").lower()
            entries[name] = (sector, size)
            for child in (left, right):
                if child not in (0, 0xFFFF):
                    pending.append(child * 4)
        found = set()
        for name in WANTED:
            if name in entries:
                sector, size = entries[name]
                iso.seek(partition + sector * SECTOR)
                with open(game / name, "wb") as out:
                    remaining = size
                    while remaining:
                        chunk = iso.read(min(remaining, 16 << 20))
                        if not chunk:
                            raise SystemExit(f"The disc image ends inside {name}")
                        out.write(chunk)
                        remaining -= len(chunk)
                found.add(name)
        return found


def main() -> None:
    url, game, downloads = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    game.mkdir(parents=True, exist_ok=True)
    downloads.mkdir(parents=True, exist_ok=True)
    target = downloads / "game-files.bin"
    print("Downloading the game files (link from the repository secret)...", flush=True)
    try:
        download(url, target)
    except SystemExit:
        raise
    except Exception as error:
        raise SystemExit(f"Download failed: {hide_link(str(error), url)}") from None
    print(f"Downloaded {target.stat().st_size / (1 << 20):.0f} MiB", flush=True)
    try:
        with open(target, "rb") as f:
            magic = f.read(512)
        if magic.startswith(b"PK\x03\x04"):
            print("Archive: zip", flush=True)
            found = from_zip(target, game)
        elif magic.startswith((b"Rar!\x1a\x07", b"7z\xbc\xaf\x27\x1c")):
            print("Archive: rar/7z", flush=True)
            try:
                found = from_7zip_archive(target, game)
            except subprocess.CalledProcessError as error:
                raise SystemExit(f"7-Zip could not extract the archive (exit code {error.returncode})") from None
        elif magic.lstrip().lower().startswith((b"<!doctype", b"<html")):
            raise SystemExit("The link returned a web page instead of the file: share the file as "
                             "'Anyone with the link' (or use a direct download link)")
        else:
            print("Disc image", flush=True)
            found = from_iso(target, game)
    finally:
        target.unlink(missing_ok=True)
    missing = [name for name in WANTED if name not in found]
    if missing:
        raise SystemExit(f"The download does not contain {', '.join(missing)}")
    digest = hashlib.sha256((game / "default.xex").read_bytes()).hexdigest().upper()
    if digest != XEX_SHA256:
        raise SystemExit(f"default.xex is not the supported revision of the game (SHA-256 {digest}, "
                         f"expected {XEX_SHA256}): the recompilation only supports one disc revision")
    for name in WANTED:
        print(f"{name}: {(game / name).stat().st_size / (1 << 20):.1f} MiB", flush=True)
    print("default.xex: supported revision", flush=True)


if __name__ == "__main__":
    main()
