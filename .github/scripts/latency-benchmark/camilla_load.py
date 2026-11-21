"""Minimal loopback-only RFC 6455 client for numeric CamillaDSP telemetry."""
import base64
import hashlib
import json
import os
import socket
import struct
import threading
import time


class LoadPoller:
    def __init__(self, port):
        self.port = port
        self.samples = []
        self.errors = 0
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._poll, daemon=True)
        self.thread.start()

    @staticmethod
    def exact(sock, count):
        data = b''
        while len(data) < count:
            block = sock.recv(count - len(data))
            if not block:
                raise ConnectionError('closed')
            data += block
        return data

    def request(self, sock):
        payload = b'"GetProcessingLoad"'
        mask = os.urandom(4)
        sock.sendall(bytes([0x81, 0x80 | len(payload)]) + mask +
                     bytes(value ^ mask[i % 4] for i, value in enumerate(payload)))
        header = self.exact(sock, 2)
        if header[0] & 0x0f != 1 or not header[0] & 0x80:
            raise ValueError('unexpected frame')
        length = header[1] & 0x7f
        if length == 126:
            length = struct.unpack('!H', self.exact(sock, 2))[0]
        elif length == 127:
            length = struct.unpack('!Q', self.exact(sock, 8))[0]
        if length > 4096 or header[1] & 0x80:
            raise ValueError('unexpected frame size or mask')
        reply = json.loads(self.exact(sock, length))
        value = reply['GetProcessingLoad']
        if value.get('result') == 'Ok' and isinstance(value.get('value'), (int, float)):
            self.samples.append(float(value['value']))

    def _poll(self):
        while not self.stop.is_set():
            try:
                with socket.create_connection(('127.0.0.1', self.port), timeout=0.5) as sock:
                    key = base64.b64encode(os.urandom(16)).decode('ascii')
                    request = (f'GET / HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\n'
                               'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                               f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n')
                    sock.sendall(request.encode('ascii'))
                    header = b''
                    while not header.endswith(b'\r\n\r\n') and len(header) < 8192:
                        header += self.exact(sock, 1)
                    expected = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
                    if not header.startswith(b'HTTP/1.1 101') or expected not in header:
                        raise ValueError('websocket handshake failed')
                    while not self.stop.is_set():
                        self.request(sock)
                        self.stop.wait(0.5)
            except (OSError, ValueError, KeyError, TypeError):
                self.errors += 1
                self.stop.wait(0.25)

    def finish(self):
        self.stop.set()
        self.thread.join(timeout=2)
        positive = [x for x in self.samples if x > 0]
        return {'samples': len(self.samples), 'positive_samples': len(positive),
                'mean_percent': sum(positive) / len(positive) if positive else None,
                'min_percent': min(positive) if positive else None,
                'max_percent': max(positive) if positive else None,
                'connection_errors': self.errors,
                'interpretation': ('Pipeline capacity estimate sampled during unpaced file processing; not realtime endpoint CPU utilization.'
                                   if positive else 'Not measured: no positive processing-load sample before file processing ended.')}
