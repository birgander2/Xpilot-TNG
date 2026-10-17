#!/usr/bin/env python3
"""
Security regression tests for the XPilot TNG server.

Starts a real server (ideally the ASan/UBSan build, see below), talks to it
over UDP like a (malicious) client and checks that it survives and behaves.

    cmake -S . -B build-asan -DSANITIZE=ON -DHARDENING=OFF -DBUILD_CLIENT_SDL3=OFF -DBUILD_LAUNCHER=OFF
    cmake --build build-asan -j
    python3 tests/server_security.py build-asan/xpilot-tng-server

Needs the loopback range 127.0.0.0/8 (Linux).
"""
import os
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP = os.path.join(ROOT, "lib", "maps", "circle2.xp2")
VERSION = 0x4F15
MAGIC = (VERSION << 16) | 0xF4ED

ENTER_GAME, ENTER_QUEUE, REPORT_STATUS, OPTION_LIST, CONTACT = 0x00, 0x01, 0x21, 0x28, 0x31
SHUTDOWN, KICK_PLAYER, LOCK_GAME = 0x64, 0x65, 0x62
E_NOT_OWNER = 0x01

PKT_VERIFY, PKT_PLAY, PKT_QUIT, PKT_RELIABLE, PKT_ACK = 1, 3, 4, 42, 43
PKT_POWER, PKT_TALK, PKT_SHAPE, PKT_MOTD, PKT_MODIFIERBANK = 34, 51, 57, 58, 71

OWNER = os.environ.get("USER") or os.environ.get("LOGNAME") or "root"
PASSWORD = "s3cret"
_port = [15400]


def s(text):
    return text.encode() + b"\0"


def contact_packet(reply_to, payload=b"", user=OWNER, magic=MAGIC):
    return struct.pack(">I", magic) + s(user) + struct.pack(">HB", 0, reply_to) + payload


class Server:
    def __init__(self, binary, *args):
        _port[0] += 1
        self.port = _port[0]
        self.tmp = tempfile.TemporaryDirectory()
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=0",
                   UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0")
        self.log = open(os.path.join(self.tmp.name, "server.log"), "w+")
        self.proc = subprocess.Popen(
            [binary, "-port", str(self.port), "-map", MAP, "-noQuit",
             "+reportToMetaServer", "-password", PASSWORD, *args],
            cwd=self.tmp.name, stdout=self.log, stderr=subprocess.STDOUT, env=env)
        for _ in range(200):
            if "frames per second" in self.output() or self.proc.poll() is not None:
                break
            time.sleep(0.1)
        time.sleep(0.2)

    def addr(self):
        return ("127.0.0.1", self.port)

    def query(self, reply_to, payload=b"", timeout=1.0, src="127.0.0.1"):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((src, 0))
        sock.sendto(contact_packet(reply_to, payload), self.addr())
        r, _, _ = select.select([sock], [], [], timeout)
        data = sock.recv(8192) if r else None
        sock.close()
        return data

    def alive(self):
        return self.proc.poll() is None

    def output(self):
        self.log.flush()
        self.log.seek(0)
        return self.log.read()

    def stop(self):
        if self.alive():
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        out = self.output()
        self.tmp.cleanup()
        return out


class Client:
    """Minimal client: queue, verify, ack the setup data, optionally play."""

    def __init__(self, server, nick, src="127.0.0.1", user=OWNER):
        self.server = server
        self.nick = nick
        self.user = user
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((src, 0))
        self.expected = 0
        self.login = None
        self.seq = 0
        self.seen = b""
        self.quit = None

    def recv(self, timeout):
        r, _, _ = select.select([self.sock], [], [], timeout)
        if not r:
            return None
        data, frm = self.sock.recvfrom(65536)
        self.seen += data
        return data, frm

    def queue(self, timeout=15):
        payload = s(self.nick) + s("localhost:0") + s("localhost") + struct.pack(">i", -1)
        end = time.time() + timeout
        last = 0
        while time.time() < end:
            if time.time() - last > 1:
                self.sock.sendto(contact_packet(ENTER_QUEUE, payload, self.user), self.server.addr())
                last = time.time()
            pkt = self.recv(0.2)
            if pkt and len(pkt[0]) >= 8:
                magic, reply_to, status = struct.unpack(">IBB", pkt[0][:6])
                if reply_to == ENTER_GAME and status == 0:
                    self.login = (pkt[1][0], struct.unpack(">H", pkt[0][6:8])[0])
                    return True
        return False

    def ack_reliable(self, data, frm):
        """Ack a datagram that starts with PKT_RELIABLE; returns payload len."""
        if not data or data[0] != PKT_RELIABLE or len(data) < 11:
            return 0
        ln, off, loops = struct.unpack(">hii", data[1:11])
        if off == self.expected:
            self.expected += ln
        self.sock.sendto(struct.pack(">Bii", PKT_ACK, self.expected, loops), frm)
        return ln

    def handshake(self):
        """Verify and receive/ack all setup data until the server is quiet."""
        assert self.queue(), "never got a login port"
        self.sock.sendto(bytes([PKT_VERIFY]) + s(self.user) + s(self.nick), self.login)
        quiet_since = time.time()
        while time.time() - quiet_since < 1.5:
            pkt = self.recv(0.2)
            if pkt:
                if self.ack_reliable(*pkt) or pkt[0][0] == PKT_QUIT:
                    quiet_since = time.time()
                if pkt[0][0] == PKT_QUIT:
                    return False
        return True

    def send(self, data):
        self.sock.sendto(data, self.login)

    def talk(self, text):
        self.seq += 1
        self.send(bytes([PKT_TALK]) + struct.pack(">i", self.seq) + s(text))

    def drain(self, duration):
        end = time.time() + duration
        quit_reason = None
        while time.time() < end:
            pkt = self.recv(0.1)
            if pkt:
                self.ack_reliable(*pkt)
                if pkt[0][0] == PKT_QUIT:
                    quit_reason = pkt[0][1:].split(b"\0")[0].decode(errors="replace")
                    self.quit = quit_reason
        return quit_reason

    def close(self):
        self.sock.close()


results = []


def check(name, ok, detail=""):
    results.append(ok)
    print(("PASS " if ok else "FAIL ") + name + (" - " + detail if detail and not ok else ""))


def sanitizer_clean(out):
    return "AddressSanitizer" not in out and "runtime error" not in out


# ---------------------------------------------------------------- tests


def attack_power(c):
    c.send(bytes([PKT_POWER]) + struct.pack(">h", 5000))


def attack_modbank(c):
    c.send(bytes([PKT_MODIFIERBANK, 0]) + s("FN"))


def attack_motd(c):
    c.send(bytes([PKT_MOTD]) + struct.pack(">ii", 0x7FFFFFFF, 512))


def attack_shape(c):
    # Two %S strings of up to 255 bytes each, together far more than the
    # 256 byte buffer the shape keyword argument used to be copied into.
    c.send(bytes([PKT_SHAPE]) + s("(shape: " + "3,3 " * 50) + s("9,9 " * 60))


def test_pre_login_packets(binary):
    """Malformed or premature packets in the login state (each on a fresh server)."""
    for name, attack in (("power before play", attack_power),
                         ("modifier bank before play", attack_modbank),
                         ("huge MOTD offset", attack_motd),
                         ("overlong shape string", attack_shape)):
        srv = Server(binary)
        try:
            c = Client(srv, "Attacker")
            if not c.handshake():
                check(name + ": handshake", False)
                continue
            attack(c)
            c.drain(0.7)
            check(name + ": server alive", srv.alive())
            c.close()
        finally:
            out = srv.stop()
        check(name + ": no sanitizer reports", sanitizer_clean(out), out[-1500:])


def test_deny_hosts(binary):
    srv = Server(binary, "-denyHosts", "10.1.1.1,10.2.2.2 10.3.0.0/255.255.0.0")
    try:
        check("denyHosts list does not corrupt the heap", srv.alive() and srv.query(REPORT_STATUS) is not None)
    finally:
        out = srv.stop()
    check("no sanitizer reports (denyHosts)", sanitizer_clean(out), out[-1500:])

    srv = Server(binary, "-denyHosts", "127.0.0.1")
    try:
        check("denied host gets no reply", srv.query(REPORT_STATUS, timeout=0.5) is None)
    finally:
        srv.stop()


def test_contact_admin(binary):
    srv = Server(binary)
    try:
        for name, pack, payload in (("shutdown", SHUTDOWN, struct.pack(">i", 0) + s("bye")),
                                    ("kick", KICK_PLAYER, s("Nobody")),
                                    ("lock", LOCK_GAME, b"")):
            reply = srv.query(pack, payload)
            ok = reply is not None and len(reply) >= 6 and reply[5] == E_NOT_OWNER
            check("privileged %s refused by default" % name, ok, repr(reply))
        check("server still running", srv.alive())
    finally:
        srv.stop()


def test_rate_limit(binary):
    srv = Server(binary, "+contactRateLocalExempt", "-contactBurst", "3", "-contactRate", "1")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for _ in range(30):
            sock.sendto(contact_packet(REPORT_STATUS), srv.addr())
        got = 0
        while select.select([sock], [], [], 0.5)[0]:
            sock.recv(8192)
            got += 1
        check("status requests are rate limited (%d of 30 answered)" % got, 1 <= got <= 6)
        sock.sendto(contact_packet(CONTACT), srv.addr())
        check("plain CONTACT is not rate limited", bool(select.select([sock], [], [], 1)[0]))
        time.sleep(3.2)
        sock.sendto(contact_packet(REPORT_STATUS), srv.addr())
        check("limit recovers after waiting", bool(select.select([sock], [], [], 1)[0]))
        c = Client(srv, "Joiner")
        check("joining works while status is limited", c.queue())
        c.close()
        sock.close()
    finally:
        srv.stop()


def test_shared_address(binary):
    """Several players behind one address (NAT, LAN) must be able to join."""
    srv = Server(binary, "-maxClientsPerIP", "5")
    try:
        clients = [Client(srv, "Lan%c" % c) for c in "ABCD"]
        for c in clients:       # one request each, no retries
            payload = s(c.nick) + s("localhost:0") + s("localhost") + struct.pack(">i", -1)
            c.sock.sendto(contact_packet(ENTER_QUEUE, payload), srv.addr())
        statuses = []
        for c in clients:
            pkt = c.recv(2.0)
            statuses.append(pkt[0][5] if pkt and len(pkt[0]) >= 6 else None)
        check("4 players from one address can queue at once", statuses == [0, 0, 0, 0], str(statuses))
        for c in clients:
            c.close()
    finally:
        srv.stop()


def test_login_hijack(binary):
    srv = Server(binary)
    try:
        c = Client(srv, "Victim")
        check("queue", c.queue())
        evil = Client(srv, "Victim", src="127.0.0.2")
        evil.login = c.login
        evil.send(bytes([PKT_VERIFY]) + s(OWNER) + s("Victim"))
        check("verify from another address is ignored", evil.recv(1.0) is None)
        c.send(bytes([PKT_VERIFY]) + s(OWNER) + s("Victim"))
        got = c.recv(2.0)
        check("legitimate verify still answered", got is not None)
        evil.close()
        c.close()
    finally:
        out = srv.stop()
    check("no sanitizer reports (hijack)", sanitizer_clean(out), out[-1500:])


def test_password_lockout(binary):
    srv = Server(binary)
    try:
        c = Client(srv, "Guesser")
        if not (c.handshake()):
            check("handshake", False)
            return
        c.send(bytes([PKT_PLAY]))
        c.drain(1.0)
        for i in range(3):
            c.talk("/password wrong%d" % i)
            c.drain(0.3)
        c.drain(3.0)
        reason = c.quit
        check("3 wrong passwords get the player kicked", reason is not None, str(reason))
        c.close()

        d = Client(srv, "Retry")
        d.handshake()
        d.send(bytes([PKT_PLAY]))
        d.drain(1.0)
        d.talk("/password " + PASSWORD)
        d.drain(1.0)
        check("correct password refused during lockout",
              b"operator status" not in d.seen.replace(b"You got", b"") and b"You got operator" not in d.seen)
        check("lockout message sent", b"Too many wrong passwords" in d.seen)
        d.close()
        check("server alive", srv.alive())
    finally:
        out = srv.stop()
    check("no sanitizer reports (password)", sanitizer_clean(out), out[-1500:])


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    for t in (test_deny_hosts, test_contact_admin, test_rate_limit, test_shared_address, test_login_hijack,
              test_pre_login_packets, test_password_lockout):
        print("== " + t.__name__)
        try:
            t(binary)
        except Exception as e:  # keep going, report as failure
            check(t.__name__ + " raised", False, repr(e))
    failed = results.count(False)
    print("\n%d checks, %d failed" % (len(results), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
