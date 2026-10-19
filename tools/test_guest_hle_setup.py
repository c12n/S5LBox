"""Validate the fixed-purpose guest installer against a private test plist."""
import os
from pathlib import Path
import plistlib
import subprocess
import sys


def main():
    binary, job = map(lambda value: Path(value).resolve(), sys.argv[1:])
    assert os.geteuid() == 0
    assert job.parent == Path.cwd() / "work/hle-launch-tests"
    assert job.name == "SpringBoard.plist"
    assert not job.exists() and not job.is_symlink()
    base = {
        "Label": "com.apple.SpringBoard", "UserName": "mobile",
        "ProgramArguments": ["/System/Library/CoreServices/SpringBoard.app/SpringBoard"],
        "KeepAlive": True, "MachServices": {"example.service": {"ResetAtClose": True}},
        "EnvironmentVariables": {"CA_ENABLE_MBX2D": "1", "DYLD_INSERT_LIBRARIES": "/usr/lib/example.dylib"},
    }
    library = "/usr/lib/s5lbox-hle-launch-v1.dylib"

    def write(value, fmt=plistlib.FMT_XML):
        job.write_bytes(plistlib.dumps(value, fmt=fmt))
        job.chmod(0o644)

    def run(mode, okay=True):
        before = job.read_bytes()
        result = subprocess.run([str(binary), mode], timeout=10, capture_output=True)
        assert (result.returncode == 0) == okay, result.stderr
        if not okay:
            assert job.read_bytes() == before
        assert not list(job.parent.glob("SpringBoard.plist.*"))

    try:
        for fmt in (plistlib.FMT_XML, plistlib.FMT_BINARY):
            write(base, fmt)
            run("enable")
            wanted = {**base, "EnvironmentVariables": {**base["EnvironmentVariables"],
                      "DYLD_INSERT_LIBRARIES": "/usr/lib/example.dylib:" + library}}
            assert plistlib.loads(job.read_bytes()) == wanted
            inode = job.stat().st_ino
            run("enable")
            assert job.stat().st_ino == inode  # Already enabled: no rewrite.
            assert job.stat().st_mode & 0o777 == 0o644 and job.stat().st_uid == 0
            run("disable")
            assert plistlib.loads(job.read_bytes()) == base
            inode = job.stat().st_ino
            run("disable")
            assert job.stat().st_ino == inode
        clean = {key: value for key, value in base.items() if key != "EnvironmentVariables"}
        write(clean); run("enable"); run("disable")
        assert plistlib.loads(job.read_bytes()) == clean
        for bad in (
            {**base, "Label": "wrong.job"},
            {**base, "EnvironmentVariables": []},
            {**base, "EnvironmentVariables": {"DYLD_INSERT_LIBRARIES": 123}},
            {**base, "UserName": "root"},
            {**base, "ProgramArguments": []},
        ):
            write(bad); run("enable", False)
        job.write_bytes(b"broken plist"); run("enable", False)
        write(base); job.chmod(0o666); run("enable", False)
        job.chmod(0o644); os.chown(job, 501, 501); run("enable", False); os.chown(job, 0, 0)
        write(base); run("invalid", False)
        real = job.parent / "unchanged-target"
        job.rename(real)
        try:
            job.symlink_to(real)
            run("enable", False)
        finally:
            job.unlink(); real.rename(job)
        print("S5LBOX-HLE-GUEST-SETUP PASS")
    finally:
        job.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
