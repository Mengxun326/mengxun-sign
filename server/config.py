"""Application configuration — override via environment variables."""
import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent

# Database — use PostgreSQL in production, SQLite for dev
DATABASE_URL = os.getenv("DATABASE_URL", f"sqlite+aiosqlite:///{BASE_DIR}/xx_sign.db")

# Security
SECRET_KEY = os.getenv("SECRET_KEY", "change-me-in-production-2024")
CREDENTIAL_ENCRYPTION_KEY = os.getenv(
    "CREDENTIAL_ENCRYPTION_KEY",
    "xxt-chaoxing-cred-encryption-key!!"
).encode()[:32].ljust(32, b'\x00')

# Chaoxing API (no change needed)
CHAOXING_LOGIN_URL = "https://passport2-api.chaoxing.com/v11/loginregister"
CHAOXING_COOKIE_URL = "https://passport2.chaoxing.com/api/cookie"
CHAOXING_COURSES_URL = "https://mooc1-2.chaoxing.com/visit/courses"
CHAOXING_ACTIVE_TASKS_URL = "https://mobilelearn.chaoxing.com/widget/pcpick/stu/index"
CHAOXING_SIGN_URL = "https://mobilelearn.chaoxing.com/pptSign/stuSignajax"
CHAOXING_USER_INFO_URL = "https://i.chaoxing.com/base"
CHAOXING_API_TOKEN = "4faa8662c59590c6f43ae9fe5b002b42"
CHAOXING_DES_KEY = "Z(AfY@XS"

# Server
HOST = os.getenv("HOST", "0.0.0.0")
PORT = int(os.getenv("PORT", "8001"))

# API Key for simple protection (optional)
API_KEY = os.getenv("API_KEY", "")
