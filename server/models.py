"""Database models — Chaoxing accounts + sign-in history."""
import datetime
from sqlalchemy import Column, Integer, String, Boolean, DateTime, Text
from database import Base


class SignLog(Base):
    __tablename__ = "sign_logs"
    id = Column(Integer, primary_key=True, autoincrement=True)
    course_name = Column(String(256), default="")
    sign_type = Column(String(32), default="")
    account_name = Column(String(128), default="")
    success = Column(Boolean, default=False)
    message = Column(Text, default="")
    created_at = Column(DateTime, default=datetime.datetime.utcnow)


class ShareToken(Base):
    __tablename__ = "share_tokens"
    id = Column(Integer, primary_key=True, autoincrement=True)
    token = Column(String(64), unique=True, nullable=False, index=True)
    course_name = Column(String(256), default="")
    sign_type = Column(String(32), default="")
    active_id = Column(String(64), default="")
    uid = Column(String(64), default="")
    fid = Column(String(64), default="0")
    cookies_json = Column(Text, default="")
    phone = Column(String(32), default="")
    password = Column(String(64), default="")
    created_at = Column(DateTime, default=datetime.datetime.utcnow)


class PendingSign(Base):
    __tablename__ = "pending_signs"
    id = Column(Integer, primary_key=True, autoincrement=True)
    token = Column(String(64), nullable=False, index=True)
    phone = Column(String(32), default="")
    password = Column(String(64), default="")
    name = Column(String(64), default="")
    status = Column(String(16), default="pending")  # pending/processing/success/failed
    result = Column(Text, default="")
    created_at = Column(DateTime, default=datetime.datetime.utcnow)


class ChaoxingAccount(Base):
    """Encrypted Chaoxing (学习通) account credentials."""
    __tablename__ = "chaoxing_accounts"

    id = Column(Integer, primary_key=True, autoincrement=True)
    # Device that owns this account — each device sees only its own accounts
    device_id = Column(String(64), nullable=False, index=True, default="")
    # Encrypted fields (AES-256, base64 encoded)
    phone_encrypted = Column(Text, nullable=False)
    password_encrypted = Column(Text, nullable=False)
    # Display name
    display_name = Column(String(256), default="")
    # Status
    is_active = Column(Boolean, default=True)
    # Cached session cookies (encrypted)
    cookies_encrypted = Column(Text, default="")
    # Metadata from Chaoxing
    real_name = Column(String(256), default="")
    uid = Column(String(64), default="")
    created_at = Column(DateTime, default=datetime.datetime.utcnow)
    updated_at = Column(DateTime, default=datetime.datetime.utcnow, onupdate=datetime.datetime.utcnow)
