"""Chaoxing (学习通) API integration.

Handles: login, course listing, active sign task detection,
and sign-in execution for all types (normal, QR, location, photo, gesture, code).
"""
import json
import re
import time
import hashlib
from typing import Optional
from dataclasses import dataclass, field
from urllib.parse import urlencode, unquote

import httpx

from config import (
    CHAOXING_LOGIN_URL, CHAOXING_COOKIE_URL, CHAOXING_COURSES_URL,
    CHAOXING_ACTIVE_TASKS_URL, CHAOXING_SIGN_URL, CHAOXING_USER_INFO_URL,
    CHAOXING_API_TOKEN, CHAOXING_DES_KEY,
)
from encryption import generate_chaoxing_inf_enc


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class ChaoxingSession:
    """Holds an active Chaoxing session (cookies + user info)."""
    cookies: dict = field(default_factory=dict)
    uid: str = ""
    name: str = ""
    school: str = ""
    phone: str = ""


@dataclass
class CourseInfo:
    """Course information."""
    course_id: str = ""
    class_id: str = ""
    name: str = ""
    teacher: str = ""


@dataclass
class ActiveSignTask:
    """An active sign-in task."""
    active_id: str = ""
    course_name: str = ""
    sign_type: str = ""  # normal, qr, location, photo, gesture, code
    course_id: str = ""
    class_id: str = ""
    raw_data: dict = field(default_factory=dict)


# ---------------------------------------------------------------------------
# HTTP client
# ---------------------------------------------------------------------------

def _create_client(cookies: dict = None) -> httpx.AsyncClient:
    """Create an httpx client — minimal headers to avoid bot detection."""
    headers = {
        "User-Agent": "Mozilla/5.0",
    }
    client = httpx.AsyncClient(headers=headers, timeout=30.0)
    if cookies:
        for name, value in cookies.items():
            client.cookies.set(name, value)
    return client


# ---------------------------------------------------------------------------
# Login
# ---------------------------------------------------------------------------

async def chaoxing_login(phone: str, password: str) -> Optional[ChaoxingSession]:
    """Login to Chaoxing with phone number and password.

    Returns a ChaoxingSession on success, or None on failure.
    """
    time_ms, inf_enc = generate_chaoxing_inf_enc()

    params = {
        "token": CHAOXING_API_TOKEN,
        "_time": time_ms,
        "inf_enc": inf_enc,
    }

    form_data = {
        "uname": phone,
        "code": password,
        "loginType": "1",
        "roleSelect": "true",
    }

    async with _create_client() as client:
        # Step 1: Login
        resp = await client.post(
            CHAOXING_LOGIN_URL,
            params=params,
            data=form_data,
        )
        if resp.status_code != 200:
            return None

        try:
            result = resp.json()
            if not result.get("status"):
                return None
        except (json.JSONDecodeError, KeyError):
            pass

        # Step 2: Get additional cookies
        await client.get(CHAOXING_COOKIE_URL)

        # Step 3: Get user info page
        resp3 = await client.get(CHAOXING_USER_INFO_URL)
        html = resp3.text

        # Build cookies dict (iterate jar to preserve all cookies)
        all_cookies = {}
        for cookie in client.cookies.jar:
            all_cookies[cookie.name] = cookie.value

        session = ChaoxingSession(cookies=all_cookies, phone=phone)

        # Extract name from HTML
        match = re.search(r'class="user-name"[^>]*>([^<]+)<', html)
        if match:
            session.name = match.group(1).strip()

        # Extract uid
        uid_match = re.search(r'"_uid"\s*:\s*"?(\d+)"?', html)
        if uid_match:
            session.uid = uid_match.group(1)
        elif "_uid" in all_cookies:
            session.uid = all_cookies["_uid"]

        return session if session.uid else None


# ---------------------------------------------------------------------------
# Course listing
# ---------------------------------------------------------------------------

async def get_courses(session: ChaoxingSession) -> list[CourseInfo]:
    """Get the list of enrolled courses."""
    courses = []
    async with _create_client(session.cookies) as client:
        resp = await client.get(CHAOXING_COURSES_URL)
        html = resp.text

        # Update cookies (handle duplicate cookies from httpx jar)
        try:
            for cookie in client.cookies.jar:
                session.cookies[cookie.name] = cookie.value
        except Exception:
            pass

        # Parse course list from HTML
        # Find all courseId+classId pairs and ALL title attributes, then match by proximity
        cid_matches = [(m.group(1), m.start()) for m in re.finditer(r'name="courseId"[^>]*value="(\d+)"', html)]
        clid_matches = [(m.group(1), m.start()) for m in re.finditer(r'name="classId"[^>]*value="(\d+)"', html)]
        title_matches = [(m.group(1), m.start()) for m in re.finditer(r'title="([^"]+)"', html)]

        if len(cid_matches) != len(clid_matches):
            print(f"[WARN] courseId count {len(cid_matches)} != classId count {len(clid_matches)}")

        # Use the shorter list length
        count = min(len(cid_matches), len(clid_matches))
        for i in range(count):
            cid, cid_pos = cid_matches[i]
            clid, clid_pos = clid_matches[i]
            # Find nearest title (within reasonable range)
            name = ""
            for title, tpos in title_matches:
                if abs(tpos - cid_pos) < 500 and title not in ('退出', '首页', '登录', '退课', '移动到', ''):
                    name = title
                    break
            if not name:
                for title, tpos in title_matches:
                    if abs(tpos - cid_pos) < 1000:
                        name = title
                        break
            if not name:
                name = "课程" + cid[-4:]

            courses.append(CourseInfo(
                course_id=cid,
                class_id=clid,
                name=name,
            ))

    return courses


# ---------------------------------------------------------------------------
# Active sign-in task detection
# ---------------------------------------------------------------------------

async def get_active_sign_tasks(session: ChaoxingSession,
                                courses: list[CourseInfo]) -> list[ActiveSignTask]:
    """Check all courses for active sign-in tasks.

    Only returns tasks from the #startList section (active/upcoming),
    filtering out completed tasks in #endList.
    Also skips courses named '退课' (dropped) or '移动到' (moved).
    """
    tasks = []
    async with _create_client(session.cookies) as client:
        for course in courses:
            try:
                url = (
                    f"{CHAOXING_ACTIVE_TASKS_URL}"
                    f"?courseId={course.course_id}"
                    f"&jclassId={course.class_id}"
                )
                resp = await client.get(url)
                html = resp.text

                # Only parse the #startList section (active tasks)
                start_match = re.search(r'id="startList"[^>]*>(.*?)id="endList"', html, re.DOTALL)
                start_html = start_match.group(1) if start_match else html

                # Find all activeDetail IDs with their positions
                active_positions = [(m.group(1), m.start())
                                    for m in re.finditer(r'activeDetail\((\d+)', start_html)]

                for active_id, pos in active_positions:
                    # Look for sign type text in the 800 chars after the activeDetail
                    forward_text = start_html[pos:pos + 800]

                    sign_type = "normal"
                    if "二维码" in forward_text or "扫码" in forward_text:
                        sign_type = "qr"
                    elif "位置" in forward_text or "定位" in forward_text:
                        sign_type = "location"
                    elif "拍照" in forward_text:
                        sign_type = "photo"
                    elif "手势" in forward_text:
                        sign_type = "gesture"
                    elif "签到码" in forward_text:
                        sign_type = "code"

                    tasks.append(ActiveSignTask(
                        active_id=active_id,
                        course_name=course.name,
                        sign_type=sign_type,
                        course_id=course.course_id,
                        class_id=course.class_id,
                    ))
            except Exception:
                continue

    return tasks


# ---------------------------------------------------------------------------
# Execute sign-in
# ---------------------------------------------------------------------------

async def execute_normal_sign(session: ChaoxingSession,
                               task: ActiveSignTask,
                               latitude: str = "-1",
                               longitude: str = "-1",
                               address: str = "") -> dict:
    """Execute a normal/gesture/code sign-in."""
    params = {
        "name": session.name,
        "address": address or "中国",
        "activeId": task.active_id,
        "uid": session.uid,
        "clientip": "",
        "latitude": latitude,
        "longitude": longitude,
        "fid": session.cookies.get("fid", "0"),
        "appType": "15",
        "ifTiJiao": "1",
    }

    async with _create_client(session.cookies) as client:
        resp = await client.get(CHAOXING_SIGN_URL, params=params)
        text = resp.text.strip()
        return {
            "success": "成功" in text or "success" in text.lower(),
            "message": text,
            "already_signed": "您已签到" in text,
        }


async def execute_location_sign(session: ChaoxingSession,
                                task: ActiveSignTask,
                                latitude: str,
                                longitude: str,
                                address: str) -> dict:
    """Execute a location-based sign-in."""
    # Location sign-in uses the same endpoint but requires valid lat/lon
    return await execute_normal_sign(
        session, task,
        latitude=latitude,
        longitude=longitude,
        address=address,
    )


async def execute_qr_sign(session: ChaoxingSession,
                          task: ActiveSignTask,
                          enc: str) -> dict:
    """Execute a QR code sign-in using the enc parameter from QR code."""
    params = {
        "enc": enc,
        "name": session.name,
        "activeId": task.active_id,
        "uid": session.uid,
        "clientip": "",
        "latitude": "-1",
        "longitude": "-1",
        "fid": session.cookies.get("fid", "0"),
        "appType": "15",
    }

    async with _create_client(session.cookies) as client:
        resp = await client.get(CHAOXING_SIGN_URL, params=params)
        text = resp.text.strip()
        return {
            "success": "成功" in text or "success" in text.lower(),
            "message": text,
            "already_signed": "您已签到" in text,
        }


async def execute_photo_sign(session: ChaoxingSession,
                             task: ActiveSignTask,
                             image_data: bytes = None) -> dict:
    """Execute a photo sign-in.

    If image_data is not provided, falls back to normal sign-in
    (teacher will see no image but sign-in succeeds).
    """
    if not image_data:
        # Fallback: sign without photo
        return await execute_normal_sign(session, task)

    # Upload image first
    async with _create_client(session.cookies) as client:
        # Upload to Chaoxing pan
        files = {"file": ("0.jpg", image_data, "image/jpeg")}
        upload_resp = await client.post(
            "https://pan-yz.chaoxing.com/upload",
            files=files,
        )
        # Then submit sign-in with image reference
        params = {
            "name": session.name,
            "activeId": task.active_id,
            "uid": session.uid,
            "clientip": "",
            "latitude": "-1",
            "longitude": "-1",
            "fid": session.cookies.get("fid", "0"),
            "appType": "15",
            "objectId": upload_resp.text.strip() if upload_resp.status_code == 200 else "",
        }
        resp = await client.get(CHAOXING_SIGN_URL, params=params)
        text = resp.text.strip()
        return {
            "success": "成功" in text or "success" in text.lower(),
            "message": text,
            "already_signed": "您已签到" in text,
        }


# ---------------------------------------------------------------------------
# Batch sign-in helper
# ---------------------------------------------------------------------------

async def batch_sign_in(sessions: list[ChaoxingSession],
                        task: ActiveSignTask,
                        sign_params: dict = None) -> list[dict]:
    """Execute sign-in for all provided sessions on the same task.

    sign_params contains: latitude, longitude, address, enc, image_data
    depending on the sign type.
    """
    sign_params = sign_params or {}
    results = []

    for session in sessions:
        try:
            if task.sign_type == "qr":
                enc = sign_params.get("enc", "")
                result = await execute_qr_sign(session, task, enc)
            elif task.sign_type == "location":
                result = await execute_location_sign(
                    session, task,
                    latitude=sign_params.get("latitude", "-1"),
                    longitude=sign_params.get("longitude", "-1"),
                    address=sign_params.get("address", "中国"),
                )
            elif task.sign_type == "photo":
                result = await execute_photo_sign(
                    session, task,
                    image_data=sign_params.get("image_data"),
                )
            else:  # normal, gesture, code
                result = await execute_normal_sign(
                    session, task,
                    latitude=sign_params.get("latitude", "-1"),
                    longitude=sign_params.get("longitude", "-1"),
                    address=sign_params.get("address", ""),
                )

            result["account_name"] = session.name
            result["account_phone"] = session.phone
            results.append(result)
        except Exception as e:
            results.append({
                "success": False,
                "message": str(e),
                "account_name": session.name,
                "account_phone": session.phone,
            })

    return results
