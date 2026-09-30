#!/usr/bin/env python3
"""Write a newc cpio archive without privileges.

cpio(1) can only record a device node that already exists, and creating one
needs CAP_MKNOD, so the archive is assembled here with the rdev fields filled
in directly.  The result is a plain newc cpio: nothing about it depends on the
machine that built it, which is the property that matters for a pinned,
reproducible root filesystem.
"""
import gzip
import os
import sys

TRAILER = "TRAILER!!!"


def _pad(n):
    return (-n) % 4


def _entry(out, name, mode, data=b"", rdev=None, ino=None, nlink=1):
    """Append one newc entry.  rdev is (major, minor) or None."""
    name_b = name.encode() + b"\0"
    buf = bytearray()
    buf += b"070701"
    fields = {
        "ino": 0 if ino is None else ino,
        "mode": mode,
        "uid": 0,
        "gid": 0,
        "nlink": nlink,
        "mtime": 0,          # zero: the archive must not embed a clock reading
        "filesize": len(data),
        "devmajor": 0,
        "devminor": 0,
        "rdevmajor": 0 if rdev is None else rdev[0],
        "rdevminor": 0 if rdev is None else rdev[1],
        "namesize": len(name_b),
        "check": 0,
    }
    for key in ("ino", "mode", "uid", "gid", "nlink", "mtime", "filesize",
                "devmajor", "devminor", "rdevmajor", "rdevminor",
                "namesize", "check"):
        buf += b"%08X" % fields[key]

    buf += name_b
    buf += b"\0" * _pad(110 + len(name_b))
    buf += data
    buf += b"\0" * _pad(len(data))
    out += buf


def build(path, entries, compress=True):
    """entries: list of (name, mode, data, rdev) tuples."""
    raw = bytearray()
    for i, (name, mode, data, rdev) in enumerate(entries, start=1):
        _entry(raw, name, mode, data, rdev, ino=i)
    # keep the ino sequence stable regardless of how many entries there are
    # Directories need nlink 2, and the trailer closes the archive.
    _entry(raw, TRAILER, 0, b"", None, ino=0, nlink=1)

    blob = bytes(raw)
    if compress:
        # mtime=0 so the gzip header carries no timestamp either.
        with gzip.GzipFile(path, "wb", compresslevel=9, mtime=0) as f:
            f.write(blob)
    else:
        with open(path, "wb") as f:
            f.write(blob)
    return len(blob)


S_IFREG = 0o100000
S_IFDIR = 0o040000
S_IFCHR = 0o020000


def entries_from(root, extra=()):
    """Walk a directory tree, then append (name, mode, data, rdev) extras."""
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        rel = os.path.relpath(dirpath, root)
        if rel == ".":
            out.append(("/", S_IFDIR | 0o755, b"", None))
        else:
            out.append(("/" + rel, S_IFDIR | 0o755, b"", None))
        for fn in sorted(filenames):
            p = os.path.join(dirpath, fn)
            r = os.path.relpath(p, root)
            with open(p, "rb") as f:
                out.append(("/" + r, S_IFREG | 0o755, f.read(), None))
    out.extend(extra)
    return out


def urbit_entries(root, vere_src, pill_src=None, vere_dst="/urbit/vere",
                  pill_dst="/urbit/pill", modules=()):
    """The whole filesystem: /init, /urbit/vere, the pill, and four nodes.

    Nothing else.  There is no shell here, so there is nothing a shell could
    reach for; that is the point of building the archive by hand.

    Three things are optional, and the reason each is optional is the same
    reason it is here at all:

      * the pill is included for a fake ship, which is defined by being handed
        one specific pinned Arvo, and absent for a comet, which fetches its own
        from the star.  Baking it in is what makes "which Arvo is this?"
        answerable;
      * the modules are the NIC driver chain out of Alpine's linux-virt
        package.  That kernel is PREEMPT_DYNAMIC -- the drivers are modules, and
        without these three it has lo and no eth0, so the network a comet needs
        does not exist rather than merely being unconfigured.  They go on disk
        uncompressed and are loaded with init_module(2), because there is no
        kmod here to decompress them;
      * /etc/resolv.conf is slirp's resolver.  A fake ship never resolves
        anything, and a comet cannot find a star without it, which showed up as
        "http: fail (15, 504): temporary failure" and a docket thread giving up
        with %retry-too-many.  urbit-init.c writes this file too; it is here so
        that the image is complete before init runs, not instead of it.

    The modes list the four device and directory nodes; /lib/modules and /etc
    only exist when there is something to put in them."""
    extra = [
        ("/proc", S_IFDIR | 0o555, b"", None),
        ("/sys", S_IFDIR | 0o555, b"", None),
        ("/dev", S_IFDIR | 0o755, b"", None),
        ("/dev/console", S_IFCHR | 0o600, b"", (5, 1)),
        ("/dev/null", S_IFCHR | 0o666, b"", (1, 3)),
        ("/tmp", S_IFDIR | 0o1777, b"", None),
        ("/urbit", S_IFDIR | 0o755, b"", None),
    ]
    with open(vere_src, "rb") as f:
        extra.append((vere_dst, S_IFREG | 0o755, f.read(), None))
    if pill_src is not None:
        with open(pill_src, "rb") as f:
            extra.append((pill_dst, S_IFREG | 0o644, f.read(), None))
    if modules:
        extra.append(("/lib", S_IFDIR | 0o755, b"", None))
        extra.append(("/lib/modules", S_IFDIR | 0o755, b"", None))
        for path in modules:
            with open(path, "rb") as f:
                data = f.read()
            extra.append(("/lib/modules/" + os.path.basename(path),
                          S_IFREG | 0o644, data, None))
        extra.append(("/etc", S_IFDIR | 0o755, b"", None))
        extra.append(("/etc/resolv.conf", S_IFREG | 0o644,
                      b"nameserver 10.0.2.3\n", None))
    return entries_from(root, extra)


if __name__ == "__main__":
    root, out_path = sys.argv[1], sys.argv[2]
    vere_src = sys.argv[3] if len(sys.argv) > 3 else None
    # pill may come next as sys.argv[4], followed by modules.  The old call
    # pattern was (root, out, vere, pill, *modules).  The new call for comets
    # does not pass a pill, so sys.argv[4] would be the first module.  A pill
    # is always a file that ends in .pill; a module ends in .ko, so distinguish
    # by extension to keep both call forms working.
    pill_src = None
    modules = []
    idx = 4
    while idx < len(sys.argv):
        p = sys.argv[idx]
        if p.endswith(".pill") and pill_src is None:
            pill_src = p
        elif p.endswith(".ko"):
            modules.append(p)
        else:
            # Try to be conservative: if it looks like a pill path, take it.
            if pill_src is None and "pill" in os.path.basename(p):
                pill_src = p
            else:
                modules.append(p)
        idx += 1
    entries = (urbit_entries(root, vere_src, pill_src, modules=modules)
               if vere_src
               else entries_from(root, [
                   ("/proc", S_IFDIR | 0o555, b"", None),
                   ("/sys", S_IFDIR | 0o555, b"", None),
                   ("/dev", S_IFDIR | 0o755, b"", None),
                   ("/dev/console", S_IFCHR | 0o600, b"", (5, 1)),
                   ("/dev/null", S_IFCHR | 0o666, b"", (1, 3)),
               ]))
    n = build(out_path, entries)
    print("initramfs: %d bytes uncompressed -> %s (%d on disk)"
          % (n, out_path, os.path.getsize(out_path)))
