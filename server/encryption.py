"""Encryption utilities for Chaoxing credentials.

Uses AES-256-CBC for encrypting stored credentials.
Transmission is secured via HTTPS + JWT.
"""
import base64
import hashlib
import json
import os
import time
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding as sym_padding

from config import CREDENTIAL_ENCRYPTION_KEY


def _get_cipher(iv: bytes):
    """Create AES-CBC cipher."""
    key = CREDENTIAL_ENCRYPTION_KEY[:32]
    return Cipher(algorithms.AES(key), modes.CBC(iv))


def encrypt_credentials(phone: str, password: str) -> dict:
    """Encrypt Chaoxing login credentials.

    Returns dict with 'phone_encrypted' and 'password_encrypted' (base64 strings).
    """
    def _encrypt(plaintext: str) -> str:
        iv = os.urandom(16)
        padder = sym_padding.PKCS7(128).padder()
        padded_data = padder.update(plaintext.encode()) + padder.finalize()
        cipher = _get_cipher(iv)
        encryptor = cipher.encryptor()
        ciphertext = encryptor.update(padded_data) + encryptor.finalize()
        # Store iv + ciphertext together
        return base64.b64encode(iv + ciphertext).decode()

    return {
        "phone_encrypted": _encrypt(phone),
        "password_encrypted": _encrypt(password),
    }


def decrypt_credentials(phone_encrypted: str, password_encrypted: str) -> dict:
    """Decrypt Chaoxing login credentials.

    Returns dict with 'phone' and 'password'.
    """
    def _decrypt(encrypted_b64: str) -> str:
        raw = base64.b64decode(encrypted_b64)
        iv = raw[:16]
        ciphertext = raw[16:]
        cipher = _get_cipher(iv)
        decryptor = cipher.decryptor()
        padded_data = decryptor.update(ciphertext) + decryptor.finalize()
        unpadder = sym_padding.PKCS7(128).unpadder()
        plaintext = unpadder.update(padded_data) + unpadder.finalize()
        return plaintext.decode()

    return {
        "phone": _decrypt(phone_encrypted),
        "password": _decrypt(password_encrypted),
    }


def encrypt_cookies(cookies: dict) -> str:
    """Encrypt cookie dictionary to base64 string."""
    iv = os.urandom(16)
    padder = sym_padding.PKCS7(128).padder()
    plaintext = json.dumps(cookies).encode()
    padded_data = padder.update(plaintext) + padder.finalize()
    cipher = _get_cipher(iv)
    encryptor = cipher.encryptor()
    ciphertext = encryptor.update(padded_data) + encryptor.finalize()
    return base64.b64encode(iv + ciphertext).decode()


def decrypt_cookies(encrypted_b64: str) -> dict:
    """Decrypt cookie string back to dictionary."""
    if not encrypted_b64:
        return {}
    raw = base64.b64decode(encrypted_b64)
    iv = raw[:16]
    ciphertext = raw[16:]
    cipher = _get_cipher(iv)
    decryptor = cipher.decryptor()
    padded_data = decryptor.update(ciphertext) + decryptor.finalize()
    unpadder = sym_padding.PKCS7(128).unpadder()
    plaintext = unpadder.update(padded_data) + unpadder.finalize()
    return json.loads(plaintext.decode())


def generate_chaoxing_inf_enc() -> tuple:
    """Generate the inf_enc signature for Chaoxing login.

    Returns (time_ms, inf_enc_md5).
    """
    from config import CHAOXING_API_TOKEN, CHAOXING_DES_KEY

    time_ms = str(int(time.time() * 1000))
    plaintext = f"token={CHAOXING_API_TOKEN}&_time={time_ms}&DESKey={CHAOXING_DES_KEY}"
    inf_enc = hashlib.md5(plaintext.encode()).hexdigest()
    return time_ms, inf_enc


def generate_heartbeat_enc(clazz_id: str, userid: str, jobid: str,
                           object_id: str, playing_time: int,
                           duration: int, clip_time: str) -> str:
    """Generate heartbeat enc signature for video playback.

    Note: This is included for completeness but primarily used for
    course watching, not sign-in.
    """
    plaintext = (
        f"[{clazz_id}][{userid}][{jobid}][{object_id}]"
        f"[{playing_time * 1000}]"
        f"[d_yHJ!$pdA~5]"
        f"[{duration * 1000}]"
        f"[{clip_time}]"
    )
    return hashlib.md5(plaintext.encode()).hexdigest()
