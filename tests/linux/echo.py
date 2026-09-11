"""Bounded TCP/UDP echo endpoints for the isolated network acceptance lab."""
import selectors
import socket

selector = selectors.DefaultSelector()
for family, address in ((socket.AF_INET, '192.0.2.2'), (socket.AF_INET6, '2001:db8:1::2')):
    for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
        for port in (19001, 19002):
            endpoint = socket.socket(family, kind)
            endpoint.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if family == socket.AF_INET6:
                endpoint.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
            endpoint.bind((address, port))
            if kind == socket.SOCK_STREAM:
                endpoint.listen()
            selector.register(endpoint, selectors.EVENT_READ, kind)
print('ready', flush=True)
while True:
    for key, _ in selector.select():
        endpoint = key.fileobj
        if key.data == socket.SOCK_DGRAM:
            payload, address = endpoint.recvfrom(65535)
            endpoint.sendto(payload, address)
        elif key.data == socket.SOCK_STREAM:
            connection, _ = endpoint.accept()
            connection.setblocking(False)
            selector.register(connection, selectors.EVENT_READ, None)
        else:
            payload = endpoint.recv(65535)
            if payload:
                endpoint.sendall(payload)
            else:
                selector.unregister(endpoint)
                endpoint.close()
