#!/usr/bin/env python3
from __future__ import annotations

import argparse
import socket
import ssl
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional
from urllib.parse import urlparse
from urllib.request import Request, urlopen

from cryptography import x509
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import dsa, ec, ed25519, ed448, padding, rsa
from cryptography.hazmat.primitives.serialization.pkcs7 import (
    load_der_pkcs7_certificates,
    load_pem_pkcs7_certificates,
)
from cryptography.x509.oid import AuthorityInformationAccessOID, ExtensionOID


USER_AGENT = "usb_hub-root-cert-fetcher/1.0"


def log(message: str) -> None:
    print(f"[fetch_root_cert] {message}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch the root CA certificate for an HTTPS website URL."
    )
    parser.add_argument("--url", required=True, help="HTTPS website URL, for example: https://example.com/api")
    parser.add_argument("--output", required=True, help="Output PEM file path")
    parser.add_argument("--timeout", type=float, default=10.0, help="Network timeout in seconds")
    return parser.parse_args()


def ensure_https_target(raw_url: str) -> tuple[str, int]:
    parsed = urlparse(raw_url)
    if parsed.scheme.lower() != "https":
        raise ValueError(f"Only HTTPS URLs are supported, got: {raw_url}")
    if not parsed.hostname:
        raise ValueError(f"URL is missing hostname: {raw_url}")
    return parsed.hostname, parsed.port or 443


def load_certificates_from_bytes(data: bytes) -> List[x509.Certificate]:
    certificates: List[x509.Certificate] = []

    for loader in (
        x509.load_pem_x509_certificate,
        x509.load_der_x509_certificate,
    ):
        try:
            certificates.append(loader(data))
            return certificates
        except ValueError:
            pass

    pem_marker = b"-----BEGIN CERTIFICATE-----"
    if pem_marker in data:
        blocks = data.split(pem_marker)
        for block in blocks:
            if not block.strip():
                continue
            candidate = pem_marker + block
            try:
                certificates.append(x509.load_pem_x509_certificate(candidate))
            except ValueError:
                continue
        if certificates:
            return certificates

    for loader in (load_der_pkcs7_certificates, load_pem_pkcs7_certificates):
        try:
            certificates = list(loader(data))
            if certificates:
                return certificates
        except ValueError:
            pass

    raise ValueError("Unable to parse certificate payload")


def fetch_server_leaf_certificate(host: str, port: int, timeout: float) -> x509.Certificate:
    context = ssl.create_default_context()
    with socket.create_connection((host, port), timeout=timeout) as tcp_socket:
        with context.wrap_socket(tcp_socket, server_hostname=host) as tls_socket:
            der_cert = tls_socket.getpeercert(binary_form=True)
    return x509.load_der_x509_certificate(der_cert)


def fetch_url_bytes(url: str, timeout: float) -> bytes:
    request = Request(url, headers={"User-Agent": USER_AGENT})
    with urlopen(request, timeout=timeout) as response:
        return response.read()


def verify_issued_by(child: x509.Certificate, issuer: x509.Certificate) -> bool:
    if child.issuer != issuer.subject:
        return False

    public_key = issuer.public_key()
    signature = child.signature
    tbs_bytes = child.tbs_certificate_bytes

    try:
        if isinstance(public_key, rsa.RSAPublicKey):
            public_key.verify(signature, tbs_bytes, padding.PKCS1v15(), child.signature_hash_algorithm)
        elif isinstance(public_key, ec.EllipticCurvePublicKey):
            public_key.verify(signature, tbs_bytes, ec.ECDSA(child.signature_hash_algorithm))
        elif isinstance(public_key, dsa.DSAPublicKey):
            public_key.verify(signature, tbs_bytes, child.signature_hash_algorithm)
        elif isinstance(public_key, (ed25519.Ed25519PublicKey, ed448.Ed448PublicKey)):
            public_key.verify(signature, tbs_bytes)
        else:
            return False
    except InvalidSignature:
        return False

    return True


def is_self_signed(cert: x509.Certificate) -> bool:
    return cert.subject == cert.issuer and verify_issued_by(cert, cert)


def extract_aia_issuer_urls(cert: x509.Certificate) -> List[str]:
    try:
        aia = cert.extensions.get_extension_for_oid(ExtensionOID.AUTHORITY_INFORMATION_ACCESS).value
    except x509.ExtensionNotFound:
        return []

    urls: List[str] = []
    for access_description in aia:
        if access_description.access_method != AuthorityInformationAccessOID.CA_ISSUERS:
            continue
        location = access_description.access_location.value
        if isinstance(location, str) and location.lower().startswith(("http://", "https://")):
            urls.append(location)
    return urls


def load_default_trust_store_certificates() -> List[x509.Certificate]:
    certificates: List[x509.Certificate] = []

    if hasattr(ssl, "enum_certificates"):
        for store_name in ("CA", "ROOT"):
            try:
                entries = ssl.enum_certificates(store_name)
            except Exception:
                continue
            for cert_bytes, encoding_type, _trust in entries:
                try:
                    if encoding_type == "x509_asn":
                        certificates.extend(load_certificates_from_bytes(cert_bytes))
                    elif encoding_type == "pkcs_7_asn":
                        certificates.extend(load_certificates_from_bytes(cert_bytes))
                except ValueError:
                    continue

    verify_paths = ssl.get_default_verify_paths()
    for path_str in (verify_paths.cafile,):
        if not path_str:
            continue
        path = Path(path_str)
        if not path.is_file():
            continue
        try:
            certificates.extend(load_certificates_from_bytes(path.read_bytes()))
        except ValueError:
            continue

    unique: Dict[bytes, x509.Certificate] = {}
    for cert in certificates:
        unique[cert.fingerprint(hashes.SHA256())] = cert
    return list(unique.values())


def find_issuer_certificate(
    cert: x509.Certificate,
    timeout: float,
    trust_store_certs: Iterable[x509.Certificate],
    aia_cache: Dict[str, List[x509.Certificate]],
) -> Optional[x509.Certificate]:
    for issuer_url in extract_aia_issuer_urls(cert):
        if issuer_url not in aia_cache:
            log(f"Downloading issuer certificate from {issuer_url}")
            aia_cache[issuer_url] = load_certificates_from_bytes(fetch_url_bytes(issuer_url, timeout))
        for candidate in aia_cache[issuer_url]:
            if verify_issued_by(cert, candidate):
                return candidate

    for candidate in trust_store_certs:
        if verify_issued_by(cert, candidate):
            return candidate

    return None


def resolve_root_certificate(leaf_cert: x509.Certificate, timeout: float) -> list[x509.Certificate]:
    trust_store_certs = load_default_trust_store_certificates()
    aia_cache: Dict[str, List[x509.Certificate]] = {}

    chain = [leaf_cert]
    seen_fingerprints = {leaf_cert.fingerprint(hashes.SHA256())}
    current = leaf_cert

    while not is_self_signed(current):
        issuer = find_issuer_certificate(current, timeout, trust_store_certs, aia_cache)
        if issuer is None:
            raise RuntimeError(
                "Unable to resolve the issuer chain to a self-signed root certificate."
            )

        fingerprint = issuer.fingerprint(hashes.SHA256())
        if fingerprint in seen_fingerprints:
            raise RuntimeError("Detected a certificate chain loop while resolving the root certificate.")

        chain.append(issuer)
        seen_fingerprints.add(fingerprint)
        current = issuer

    return chain


def write_pem_certificate(cert: x509.Certificate, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    pem_data = cert.public_bytes(serialization.Encoding.PEM)
    output_path.write_bytes(pem_data)


def main() -> int:
    args = parse_args()

    try:
        host, port = ensure_https_target(args.url)
        output_path = Path(args.output).resolve()

        log(f"Connecting to {host}:{port}")
        leaf_cert = fetch_server_leaf_certificate(host, port, args.timeout)
        chain = resolve_root_certificate(leaf_cert, args.timeout)
        root_cert = chain[-1]

        write_pem_certificate(root_cert, output_path)

        log(f"Resolved chain length: {len(chain)}")
        for index, cert in enumerate(chain, start=1):
            log(
                f"Chain[{index}] subject={cert.subject.rfc4514_string()} issuer={cert.issuer.rfc4514_string()}"
            )
        log(f"Root certificate written to: {output_path}")
        return 0
    except Exception as exc:
        log(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
