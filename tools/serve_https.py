# /// script
# requires-python = ">=3.12"
# dependencies = ["cryptography>=43"]
# ///
"""Serves web/ over https on the local network, for the phone page.

A phone's browser gives the microphone and the motion sensors only to a secure page, so the
page is served with a self-signed certificate for this PC's addresses, made once into
build/https/ and made again when the PC's address changes. The phone warns about the
certificate on the first visit; Advanced, then Proceed, opens the page.
"""

import argparse
import contextlib
import datetime
import functools
import http.server
import ipaddress
import socket
import ssl
from pathlib import Path
from typing import ClassVar

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

ROOT = Path(__file__).resolve().parent.parent
CERT_DIR = ROOT / "build" / "https"

# Windows takes the types from its registry, where .js may be text/plain: a module refuses that.
TYPES = {
    ".html": "text/html",
    ".js": "text/javascript",
    ".mjs": "text/javascript",
    ".wasm": "application/wasm",
    ".css": "text/css",
    ".json": "application/json",
}


def primary_address() -> str | None:
    """The address this PC reaches the network from (no packet is sent)."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("192.0.2.1", 9))
            address: str = s.getsockname()[0]
        except OSError:
            return None
        return address


def addresses() -> list[str]:
    """Every IPv4 address of this PC, the primary first."""
    found: list[str] = []
    first = primary_address()
    if first:
        found.append(first)
    for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
        # an IPv4 address is a string; the type allows an IPv6 scope's integer
        ip = str(info[4][0])
        if ip not in found and not ip.startswith("127."):
            found.append(ip)
    return found


def cert_covers(cert_path: Path, ips: list[str]) -> bool:
    if not cert_path.exists():
        return False
    cert = x509.load_pem_x509_certificate(cert_path.read_bytes())
    if cert.not_valid_after_utc < datetime.datetime.now(datetime.UTC) + datetime.timedelta(days=7):
        return False
    san = cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
    held = {str(ip) for ip in san.get_values_for_type(x509.IPAddress)}
    return set(ips) <= held


def make_cert(cert_path: Path, key_path: Path, ips: list[str]) -> None:
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "pan flute, local")])
    now = datetime.datetime.now(datetime.UTC)
    names: list[x509.GeneralName] = [
        x509.DNSName("localhost"),
        x509.DNSName(socket.gethostname()),
    ]
    names += [x509.IPAddress(ipaddress.ip_address(ip)) for ip in ["127.0.0.1", *ips]]
    cert = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(days=365))
        .add_extension(x509.SubjectAlternativeName(names), critical=False)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .sign(key, hashes.SHA256())
    )
    cert_path.parent.mkdir(parents=True, exist_ok=True)
    key_path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))


class DualStackServer(http.server.ThreadingHTTPServer):
    """IPv4 and IPv6 both: Windows tries localhost's ::1 first and waits 2 s for an IPv4 server."""

    address_family = socket.AF_INET6

    def server_bind(self) -> None:
        self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        super().server_bind()


class Handler(http.server.SimpleHTTPRequestHandler):
    # a class variable (ruff's RUF012) over the base's instance annotation (mypy's)
    extensions_map: ClassVar[dict[str, str]] = {  # type: ignore[misc]
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        **TYPES,
    }

    def end_headers(self) -> None:
        # every reload fetches the page afresh: the phone keeps nothing stale
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8443)
    args = parser.parse_args()

    ips = addresses()
    cert_path = CERT_DIR / "cert.pem"
    key_path = CERT_DIR / "key.pem"
    if not cert_covers(cert_path, ips):
        make_cert(cert_path, key_path, ips)
        print(f"A new certificate in {CERT_DIR}: the phone warns once more.")

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert_path, key_path)
    handler = functools.partial(Handler, directory=str(ROOT / "web"))
    server = DualStackServer(("::", args.port), handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)

    print("On the phone, on the same Wi-Fi, open:")
    for ip in ips:
        print(f"  https://{ip}:{args.port}/phone.html")
    print(f"On this PC: https://localhost:{args.port}/   (Ctrl+C stops)")
    with contextlib.suppress(KeyboardInterrupt):
        server.serve_forever()


if __name__ == "__main__":
    main()
