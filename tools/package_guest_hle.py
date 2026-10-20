"""Build the small open-source guest launch-guard package, without dpkg tools."""
import io
import pathlib
import sys
import tarfile
import plistlib


def archive(entries):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        directories = set()
        for name, data, mode in entries:
            for parent in reversed(pathlib.PurePosixPath(name).parents):
                if str(parent) == "." or parent in directories:
                    continue
                directory = tarfile.TarInfo("./" + str(parent) + "/")
                directory.type, directory.mode = tarfile.DIRTYPE, 0o755
                directory.uid = directory.gid = directory.mtime = 0
                directory.uname = directory.gname = "root"
                tar.addfile(directory)
                directories.add(parent)
            info = tarfile.TarInfo(name)
            info.mode, info.uid, info.gid, info.mtime = mode, 0, 0, 0
            info.uname = info.gname = "root"
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    import gzip
    return gzip.compress(output.getvalue(), mtime=0)


def member(name, data):
    header = f"{name + '/':<16}{0:<12}{0:<6}{0:<6}{'100644':<8}{len(data):<10}`\n".encode("ascii")
    assert len(header) == 60
    return header + data + (b"\n" if len(data) % 2 else b"")


def main():
    build, output = map(pathlib.Path, sys.argv[1:])
    repo = pathlib.Path(__file__).resolve().parent.parent
    control = (
        "Package: com.j0shua.s5lbox.hle-launch\nVersion: 1.1.0\n"
        "Architecture: iphoneos-arm\n"
        "Maintainer: j0shua-SYSON <219831225+j0shua-SYSON@users.noreply.github.com>\n"
        "Depends: firmware (= 3.1.3)\nSection: Tweaks\n"
        "Name: S5LBox touchHLE Launch Guard\n"
        "Description: Coordinates native and interpreted guest app launches.\n"
    ).encode()
    job = "/Library/LaunchDaemons/com.j0shua.s5lbox.hle-routes.plist"
    scripts = [
        ("postinst", (
            '#!/bin/sh\nset -e\n[ "$1" = configure ] || exit 0\n'
            '/usr/libexec/s5lbox-hle-setup-v1 enable\n'
            f'/bin/launchctl unload {job} >/dev/null 2>&1 || true\n'
            f'/bin/launchctl load {job}\n').encode(), 0o755),
        ("prerm", (
            '#!/bin/sh\nset -e\ncase "$1" in remove|deconfigure)\n'
            f'/bin/launchctl unload {job} >/dev/null 2>&1 || true\n'
            '/usr/libexec/s5lbox-hle-setup-v1 disable;; esac\n').encode(), 0o755),
    ]
    contents = archive([
        ("./usr/lib/s5lbox-hle-launch-v1.dylib", (build / "s5lbox-hle-launch-v1.dylib").read_bytes(), 0o755),
        ("./usr/libexec/s5lbox-hle-setup-v1", (build / "s5lbox-hle-setup-v1").read_bytes(), 0o755),
        ("./usr/libexec/s5lbox-hle-routes-v1", (build / "s5lbox-hle-routes-v1").read_bytes(), 0o755),
        ("." + job, plistlib.dumps({
            "Label": "com.j0shua.s5lbox.hle-routes",
            "ProgramArguments": ["/usr/libexec/s5lbox-hle-routes-v1"],
            "RunAtLoad": True, "KeepAlive": True, "ThrottleInterval": 2,
        }), 0o644),
        ("./usr/share/doc/com.j0shua.s5lbox.hle-launch/copyright", (repo / "LICENSE").read_bytes(), 0o644),
        ("./usr/share/doc/com.j0shua.s5lbox.hle-launch/NOTICE", (repo / "integrations/touchhle-cpu/guest-launch-NOTICE.txt").read_bytes(), 0o644),
        ("./usr/share/doc/com.j0shua.s5lbox.hle-launch/APSL-2.0.txt", (repo / "app/Resources/GuestPackages/APSL-2.0.txt").read_bytes(), 0o644),
    ])
    output.write_bytes(b"!<arch>\n" + member("debian-binary", b"2.0\n") +
                       member("control.tar.gz", archive([("control", control, 0o644), *scripts])) +
                       member("data.tar.gz", contents))
    print(f"Guest launch package: {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
