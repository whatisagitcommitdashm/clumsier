"""Bounded, explicitly authorized development-helper background startup.

Not an installer/service manager: invoked only by the sudo-authorized helper.
"""
import os
import select
import signal
import stat


def open_log(path):
    flags = os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW
    fd = os.open(path, flags, 0o600)
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o077 or info.st_nlink != 1:
        os.close(fd)
        raise RuntimeError('Refusing unsafe helper log; expected root-owned regular file, mode 600')
    return fd


def background(log_path, run, timeout=15):
    """Return in parent only after child reports socket readiness; errors stay visible."""
    log = open_log(log_path)
    reader, writer = os.pipe()
    pid = os.fork()
    if pid:
        os.close(writer)
        os.close(log)
        try:
            readable, _, _ = select.select([reader], [], [], timeout)
            message = os.read(reader, 1024) if readable else b''
            if message != b'READY\n':
                if not message:
                    try:
                        os.kill(pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                os.waitpid(pid, 0)
                detail = message.decode('utf-8', errors='replace').strip()
                raise RuntimeError(f'{detail or "Background helper did not become ready"}. Recovery log: {log_path}')
        finally:
            os.close(reader)
        print(f'Background helper ready (PID {pid}). Recovery log: {log_path}', flush=True)
        return
    os.close(reader)
    status = 1
    ready_fd = writer
    try:
        os.setsid()
        os.chdir('/')
        null = os.open('/dev/null', os.O_RDONLY)
        os.dup2(null, 0)
        os.close(null)
        os.dup2(log, 1)
        os.dup2(log, 2)
        os.close(log)
        def ready():
            nonlocal ready_fd
            os.write(ready_fd, b'READY\n')
            os.close(ready_fd)
            ready_fd = None
        run(ready)
        status = 0
    except BaseException as error:
        if ready_fd is not None:
            try:
                os.write(ready_fd, ('Helper startup failed: ' + str(error))[:900].encode('utf-8'))
            except OSError:
                pass
        import traceback
        traceback.print_exc()
    finally:
        if ready_fd is not None:
            os.close(ready_fd)
        import sys
        sys.stdout.flush()
        sys.stderr.flush()
    os._exit(status)
