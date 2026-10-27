#!/usr/bin/env python3
"""Black-box wire tests using only Python's standard library."""
import concurrent.futures
import contextlib
import json
import os
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

NODE, CLI = sys.argv[1:3]
sys.argv = [sys.argv[0]]
HEADER = struct.Struct("!4sBBHQII")

def frame(op, ident=1, key=b"", value=b""):
    return HEADER.pack(b"ATLS", 1, 0, op, ident, len(key), len(value)) + key + value

def exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError("connection closed")
        data += chunk
    return data

def response(sock):
    magic, version, kind, status, ident, ks, vs = HEADER.unpack(exact(sock, HEADER.size))
    if (magic, version, kind, ks) != (b"ATLS", 1, 1, 0) or vs > 262144:
        raise AssertionError("malformed response")
    return status, ident, exact(sock, vs)

@contextlib.contextmanager
def node(*options):
    process = subprocess.Popen([NODE, "--memory-demo", "--port", "0", *options], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        ready, _, _ = select.select([process.stdout], [], [], 10)
        if not ready:
            raise RuntimeError("node startup deadline")
        line = process.stdout.readline()
        if not line.startswith("ATLAS_READY "):
            raise RuntimeError("node did not become ready: " + line)
        port = int(line.split()[1].rsplit(":", 1)[1])
        yield port
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
        try:
            _, stderr = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise AssertionError("node failed graceful shutdown")
        if process.returncode != 0:
            raise AssertionError(f"node exited {process.returncode}: {stderr}")
        if "Sanitizer" in stderr or "runtime error:" in stderr:
            raise AssertionError(stderr)

class WireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.context = node()
        cls.port = cls.context.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.context.__exit__(None, None, None)

    def connect(self, port=None):
        return socket.create_connection(("127.0.0.1", port or self.port), timeout=3)

    def call(self, op, key=b"", value=b"", ident=1):
        with self.connect() as sock:
            sock.sendall(frame(op, ident, key, value))
            return response(sock)

    def test_fragmented_header_and_body(self):
        with self.connect() as sock:
            for byte in frame(3, 55, b"fragment", b"binary\0value"):
                sock.sendall(bytes([byte]))
            self.assertEqual(response(sock), (0, 55, b""))
        self.assertEqual(self.call(2, b"fragment")[2], b"binary\0value")

    def test_coalesced_frames_stay_ordered(self):
        with self.connect() as sock:
            sock.sendall(frame(3, 1, b"pipe", b"v") + frame(2, 2, b"pipe") + frame(4, 3, b"pipe") + frame(2, 4, b"pipe"))
            self.assertEqual(response(sock), (0, 1, b""))
            self.assertEqual(response(sock), (0, 2, b"v"))
            self.assertEqual(response(sock), (0, 3, b""))
            self.assertEqual(response(sock)[0], 1)

    def test_empty_and_maximum_values(self):
        self.assertEqual(self.call(3, b"empty", b"")[0], 0)
        self.assertEqual(self.call(2, b"empty")[2], b"")
        value = bytes(range(256)) * 64
        self.assertEqual(self.call(3, b"k" * 256, value)[0], 0)
        self.assertEqual(self.call(2, b"k" * 256)[2], value)
        self.assertEqual(self.call(3, b"oversize-value", b"x" * 16385)[0], 2)

    def test_unknown_op_is_error_without_disconnect(self):
        with self.connect() as sock:
            sock.sendall(frame(999, 12) + frame(1, 13))
            self.assertEqual(response(sock)[:2], (2, 12))
            self.assertEqual(response(sock), (0, 13, b"PONG"))

    def test_bad_headers_close_connection(self):
        candidates = [
            HEADER.pack(b"NOPE", 1, 0, 1, 1, 0, 0),
            HEADER.pack(b"ATLS", 2, 0, 1, 1, 0, 0),
            HEADER.pack(b"ATLS", 1, 1, 0, 1, 0, 0),
            HEADER.pack(b"ATLS", 1, 0, 1, 0, 0, 0),
            HEADER.pack(b"ATLS", 1, 0, 3, 1, 0xffffffff, 0xffffffff),
            HEADER.pack(b"ATLS", 1, 0, 3, 1, 257, 0),
        ]
        for data in candidates:
            with self.subTest(data=data), self.connect() as sock:
                sock.sendall(data)
                try:
                    self.assertEqual(sock.recv(1), b"")
                except ConnectionResetError:
                    pass
        self.assertEqual(self.call(1)[2], b"PONG")

    def test_disconnect_mid_frame_does_not_affect_other_clients(self):
        with self.connect() as sock:
            sock.sendall(frame(3, 1, b"unfinished", b"v")[:27])
        self.assertEqual(self.call(1)[0], 0)
        self.assertEqual(self.call(2, b"unfinished")[0], 1)

    def test_batch_all_or_nothing_validation(self):
        def mutation(op, key, value=b""):
            return struct.pack("!HII", op, len(key), len(value)) + key + value
        valid = struct.pack("!I", 2) + mutation(3, b"batch-a", b"v") + mutation(3, b"batch-b", b"w")
        self.assertEqual(self.call(5, value=valid)[0], 0)
        self.assertEqual(self.call(2, b"batch-b")[2], b"w")
        invalid = struct.pack("!I", 2) + mutation(4, b"batch-a") + mutation(999, b"batch-b")
        self.assertEqual(self.call(5, value=invalid)[0], 2)
        self.assertEqual(self.call(2, b"batch-a")[2], b"v")

    def test_concurrent_clients(self):
        def work(index):
            key = f"concurrent-{index}".encode()
            with self.connect() as sock:
                for n in range(20):
                    value = f"{index}-{n}".encode()
                    sock.sendall(frame(3, n * 2 + 1, key, value))
                    if response(sock)[0] != 0:
                        raise AssertionError("write failed")
                    sock.sendall(frame(2, n * 2 + 2, key))
                    if response(sock)[2] != value:
                        raise AssertionError("read mismatch")
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
            list(pool.map(work, range(24)))

    def test_stats_are_real_counters(self):
        before = json.loads(self.call(6)[2])
        self.call(1)
        after = json.loads(self.call(6)[2])
        self.assertGreaterEqual(after["requests"], before["requests"] + 2)
        self.assertEqual(after["role"], "standalone")

    def test_cli_and_binary_file(self):
        def cli(*args):
            return subprocess.run([CLI, "--port", str(self.port), *args], capture_output=True, timeout=5)
        self.assertEqual(cli("ping").stdout, b"PONG\n")
        self.assertEqual(cli("put", "cli", "value").returncode, 0)
        self.assertEqual(cli("get", "cli").stdout, b"value")
        self.assertEqual(cli("batch", "put", "cli", "new", "put", "other", "x").returncode, 0)
        self.assertEqual(cli("get", "cli").stdout, b"new")
        self.assertEqual(cli("delete", "cli").returncode, 0)
        self.assertEqual(cli("get", "cli").returncode, 2)
        with tempfile.NamedTemporaryFile() as file:
            file.write(b"\0\xff\n"); file.flush()
            self.assertEqual(cli("put-file", "file", file.name).returncode, 0)
            self.assertEqual(cli("get", "file").stdout, b"\0\xff\n")

    def test_deadline_closes_incomplete_frame(self):
        with node("--timeout-ms", "150") as port, self.connect(port) as sock:
            sock.sendall(b"ATL")
            self.assertEqual(sock.recv(1), b"")

    def test_connection_admission_limit(self):
        with node("--max-connections", "1") as port, self.connect(port) as first:
            first.sendall(frame(1)); self.assertEqual(response(first)[0], 0)
            with self.connect(port) as second:
                try:
                    self.assertEqual(second.recv(1), b"")
                except ConnectionResetError:
                    pass
            first.sendall(frame(6, 2))
            self.assertGreaterEqual(json.loads(response(first)[2])["rejected_connections"], 1)

    def test_explicit_demo_mode_required(self):
        process = subprocess.run([NODE], capture_output=True, timeout=3)
        self.assertNotEqual(process.returncode, 0)
        self.assertIn(b"--memory-demo", process.stderr)

    def test_shutdown_with_partial_clients(self):
        sockets = []
        try:
            with node() as port:
                for _ in range(8):
                    sock = self.connect(port); sock.sendall(b"ATL"); sockets.append(sock)
        finally:
            for sock in sockets:
                sock.close()

    def test_client_rejects_mismatched_response_id(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0)); listener.listen(1); listener.settimeout(3)
            failures = []
            def fake_server():
                try:
                    with listener.accept()[0] as sock:
                        sock.settimeout(3)
                        exact(sock, 24)
                        sock.sendall(HEADER.pack(b"ATLS", 1, 1, 0, 999, 0, 4) + b"PONG")
                except Exception as error:
                    failures.append(error)
            thread = threading.Thread(target=fake_server)
            thread.start()
            try:
                process = subprocess.run([CLI, "--port", str(listener.getsockname()[1]), "ping"], capture_output=True, timeout=5)
                self.assertEqual(process.returncode, 1)
                self.assertIn(b"correlation mismatch", process.stderr)
            finally:
                thread.join(timeout=4)
            self.assertFalse(thread.is_alive())
            self.assertEqual(failures, [])

    def test_client_deadline_when_response_is_incomplete(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0)); listener.listen(1); listener.settimeout(3)
            failures = []
            def fake_server():
                try:
                    with listener.accept()[0] as sock:
                        sock.settimeout(3)
                        exact(sock, 24)
                        sock.sendall(HEADER.pack(b"ATLS", 1, 1, 0, 1, 0, 4) + b"P")
                        # Wait for the client's deadline to close the socket.
                        self.assertEqual(sock.recv(1), b"")
                except Exception as error:
                    failures.append(error)
            thread = threading.Thread(target=fake_server)
            thread.start()
            try:
                process = subprocess.run([CLI, "--port", str(listener.getsockname()[1]), "--timeout-ms", "150", "ping"], capture_output=True, timeout=5)
                self.assertEqual(process.returncode, 1)
                self.assertIn(b"deadline exceeded", process.stderr)
            finally:
                thread.join(timeout=4)
            self.assertFalse(thread.is_alive())
            self.assertEqual(failures, [])

if __name__ == "__main__":
    unittest.main(verbosity=2)
