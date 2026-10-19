# Live application files

This component is under development and is not yet enabled in S5LBox.
It operates **inside the emulated guest**, never against the physical iPhone's
filesystem. The guest kernel remains the sole HFS writer. No mirror is copied
back into a running disk image.

The authenticated package helper will fork a child, close inherited file
descriptors except its connection, then call `s5lbox_hle_files`. Authentication
and launch coordination must be connected before exposing it to the app.
The child reads a 36-byte application-container UUID, takes a single-session
lease, enters that installed application's directory with `chroot`, and drops
all supplementary groups and root privileges. It replies with an empty result
on success. It must exit after returning; it cannot rejoin the privileged
package-service loop. The lease is not yet a SpringBoard launch exclusion.

All integers are big-endian. A request is `u32 length, u8 operation, payload`.
The length includes the operation byte and is limited to 65,536. A response is
`u32 length, u32 guest_errno, payload`; errors have no payload. Paths are
`u32 byte_length, UTF-8 bytes`, absolute within the app container, under 1,024
bytes, without NUL. Handles are opaque monotonically allocated `u32` values;
at most 64 may be open. A broken connection invalidates all handles and locks.
Never automatically replay a mutation after losing its response.

| Op | Request payload | Success payload |
| --- | --- | --- |
| 1 stat | path | u64 size, i64 mtime, u32 mode/uid/gid, u64 inode |
| 2 open | u32 flags, path | u32 handle |
| 3 close | handle | empty |
| 4 read | handle, u32 count | up to count bytes |
| 5 write | handle, bytes | u32 bytes written |
| 6 seek | handle, u32 whence, i64 offset | u64 position |
| 7 sync | handle | empty |
| 8 truncate | handle, u64 length | empty |
| 9 mkdir | path | empty |
| 10 unlink | path | empty |
| 11 rename | source path, destination path | empty |
| 12 readdir | directory handle | u32 count, repeated length-prefixed names |
| 13 lock | handle, u32 kind, u64 start/length | empty |
| 14 opendir | u32 zero, path | u32 directory handle |
| 15 rmdir | path | empty |
| 16 query lock | same as lock | u32 kind, u64 start/length, u32 pid |

Open flags: read=1, write=2, append=4, create=8, truncate=16, exclusive=32.
Whence: start=0, current=1, end=2. Lock kind: shared=1, exclusive=2, unlock=3.
Locks are real, nonblocking POSIX byte-range locks, with ordinary process/fd
lifetime semantics, including release on disconnect. A zero lock length means
through EOF. Directory batches contain up to 128 names; zero means EOF.

The chroot deliberately prevents symlinks from reaching outside the selected
container. Absolute symlinks using the original full container path are not
remapped. System library resources remain the interpreter's read-only overlay.
Invalid payloads receive errors; oversized or truncated frames close the stream.
An idle authenticated stream stays open during pause; a partial frame times out.
