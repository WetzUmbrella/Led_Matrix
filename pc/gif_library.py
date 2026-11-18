#!/usr/bin/env python3
"""The website's GIF library (step 4e, decided 18 Nov 2026 with Cheston).

GIFs uploaded from the browser are kept in ``pc/gifs/`` (gitignored), so
they survive a restart of the website. A name is a plain file name ending in
``.gif``: letters, digits, space, dot, dash and underscore, at most 64
characters, so it can never leave the folder (``folder`` defaults to
``GIF_DIR``, read at each call). A file is accepted only when Pillow opens
it as a GIF; whether it fits the frame limit is checked when it is built
for the wall (``asset_builder``).
"""

from __future__ import annotations

import os
import re
import sys
from typing import Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asset_builder as ab  # noqa: E402

#: Where the GIFs are kept (gitignored).
GIF_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gifs")

#: Largest GIF accepted, bytes (a 600-frame demo GIF is well under 1 MB).
MAX_BYTES = 8 * 1024 * 1024

#: A safe file name ending in .gif, 64 characters at most.
NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._-]{0,59}\.gif$",
                          re.IGNORECASE)

#: The first bytes of a GIF file.
GIF_MAGIC = (b"GIF87a", b"GIF89a")


class LibraryError(ab.AssetError):
    """A name or file the library refuses (the website answers 400)."""


def check_name(name: str) -> str:
    """``name`` when it is a safe GIF file name; LibraryError otherwise."""
    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise LibraryError("a GIF name is letters, digits, space, dot, dash "
                           "or underscore, ending in .gif (64 at most)")
    return name


def path_of(name: str, folder: Optional[str] = None) -> str:
    """The path of a GIF in the library; LibraryError when there is none."""
    path = os.path.join(folder or GIF_DIR, check_name(name))
    if not os.path.isfile(path):
        raise LibraryError("no GIF called %s in the library" % name)
    return path


def describe(path: str) -> Dict[str, object]:
    """Name, size in bytes, frame count and pixel size of a GIF."""
    from PIL import Image
    with Image.open(path) as gif:
        frames = getattr(gif, "n_frames", 1)
        width, height = gif.size
    return {"name": os.path.basename(path), "bytes": os.path.getsize(path),
            "frames": frames, "width": width, "height": height}


def list_gifs(folder: Optional[str] = None) -> List[Dict[str, object]]:
    """Every readable GIF in the library, by name."""
    folder = folder or GIF_DIR
    if not os.path.isdir(folder):
        return []
    found = []
    for name in sorted(os.listdir(folder), key=str.lower):
        if not NAME_PATTERN.match(name):
            continue
        try:
            found.append(describe(os.path.join(folder, name)))
        except (OSError, ValueError):
            continue  # Not readable as a GIF: left out, not deleted.
    return found


def save_gif(name: str, data: bytes,
             folder: Optional[str] = None) -> Dict[str, object]:
    """Keep ``data`` as ``name`` (replacing a GIF of that name); returns its
    description. Written to a temporary file first, so a failed save never
    leaves half a GIF in the library."""
    from PIL import Image
    folder = folder or GIF_DIR
    check_name(name)
    if not data:
        raise LibraryError("the file is empty")
    if len(data) > MAX_BYTES:
        raise LibraryError("a GIF may be at most %d MB" % (
            MAX_BYTES // (1024 * 1024)))
    if not data.startswith(GIF_MAGIC):
        raise LibraryError("%s is not a GIF file" % name)
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, name)
    temp = path + ".part"
    with open(temp, "wb") as handle:
        handle.write(data)
    try:
        with Image.open(temp) as gif:
            gif.seek(0)
            gif.convert("RGB")
    except (OSError, ValueError) as err:
        os.remove(temp)
        raise LibraryError("%s cannot be read as a GIF: %s" % (name, err))
    os.replace(temp, path)
    return describe(path)


def delete_gif(name: str, folder: Optional[str] = None) -> None:
    """Remove a GIF from the library."""
    os.remove(path_of(name, folder))
