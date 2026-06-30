"""XXT Sign Server — encrypted Chaoxing account storage.

All crypto is client-side. Server stores encrypted blobs only, never sees plaintext.
"""
from contextlib import asynccontextmanager
from fastapi import FastAPI, HTTPException, Form, File, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select
import datetime
import base64
import io
import numpy as np
from PIL import Image

from config import HOST, PORT
from database import init_db, async_session
from models import ChaoxingAccount, ShareToken, PendingSign, SignLog


SHARE_TTL_MINUTES = 5

async def _cleanup_expired_shares():
    """Delete ShareTokens and PendingSigns older than TTL."""
    from sqlalchemy import delete as _delete
    cutoff = datetime.datetime.utcnow() - datetime.timedelta(minutes=SHARE_TTL_MINUTES)
    async with async_session() as db:
        # Find expired share tokens
        result = await db.execute(
            select(ShareToken.token).where(ShareToken.created_at < cutoff)
        )
        expired_tokens = [row[0] for row in result.all()]
        if expired_tokens:
            await db.execute(_delete(PendingSign).where(PendingSign.token.in_(expired_tokens)))
            await db.execute(_delete(ShareToken).where(ShareToken.created_at < cutoff))
            await db.commit()

@asynccontextmanager
async def lifespan(app: FastAPI):
    await init_db()
    await _cleanup_expired_shares()
    print(f"[OK] Chaoxing Sign Server on {HOST}:{PORT}")
    yield


app = FastAPI(
    title="XXT Sign Server",
    description="学习通批量签到 — 直接使用学习通账号",
    version="2.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ---------------------------------------------------------------------------
# Account management (encrypted storage only — no Chaoxing access)
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------

@app.post("/api/accounts/add")
async def add_account(
    phone_encrypted: str = Form(...),
    password_encrypted: str = Form(...),
    display_name: str = Form(""),
    uid: str = Form(""),
    cookies_encrypted: str = Form(""),
    device_id: str = Form(""),
):
    """Store encrypted Chaoxing credentials, scoped to the requesting device."""
    async with async_session() as db:
        acc = ChaoxingAccount(
            device_id=device_id,
            phone_encrypted=phone_encrypted,
            password_encrypted=password_encrypted,
            display_name=display_name or uid or "用户",
            cookies_encrypted=cookies_encrypted,
            real_name=display_name,
            uid=uid,
        )
        db.add(acc)
        await db.commit()
        await db.refresh(acc)
        return {"id": acc.id, "display_name": acc.display_name, "uid": acc.uid}


@app.get("/api/accounts/list")
async def list_accounts(device_id: str = ""):
    """List stored accounts for this device — returns encrypted blobs."""
    async with async_session() as db:
        result = await db.execute(
            select(ChaoxingAccount).where(
                ChaoxingAccount.is_active == True,
                ChaoxingAccount.device_id == device_id,
            )
        )
        return [
            {
                "id": acc.id,
                "display_name": acc.display_name,
                "real_name": acc.real_name,
                "phone_encrypted": acc.phone_encrypted,
                "password_encrypted": acc.password_encrypted,
                "uid": acc.uid,
                "created_at": acc.created_at.isoformat() if acc.created_at else None,
            }
            for acc in result.scalars().all()
        ]


@app.delete("/api/accounts/{account_id}")
async def delete_account(account_id: int, device_id: str = ""):
    """Deactivate a stored account (device-scoped)."""
    async with async_session() as db:
        acc = await db.get(ChaoxingAccount, account_id)
        if not acc:
            raise HTTPException(404, detail="账号不存在")
        if device_id and acc.device_id != device_id:
            raise HTTPException(403, detail="无权操作此账号")
        acc.is_active = False
        await db.commit()
    return {"message": "账号已删除"}


# ---------------------------------------------------------------------------
# Sign-in history
# ---------------------------------------------------------------------------

@app.post("/api/sign/log")
async def log_sign_result(
    course_name: str = Form(""),
    sign_type: str = Form("normal"),
    account_name: str = Form(""),
    success: bool = Form(False),
    message: str = Form(""),
):
    """Record a sign-in result."""
    async with async_session() as db:
        log = SignLog(
            course_name=course_name,
            sign_type=sign_type,
            account_name=account_name,
            success=success,
            message=message,
        )
        db.add(log)
        await db.commit()
    return {"status": "ok"}


@app.get("/api/sign/history")
async def get_sign_history(limit: int = 50):
    """Get recent sign-in history."""
    from models import SignLog
    from sqlalchemy import desc
    async with async_session() as db:
        result = await db.execute(
            select(SignLog).order_by(desc(SignLog.created_at)).limit(limit)
        )
        return [
            {
                "id": log.id,
                "course_name": log.course_name,
                "sign_type": log.sign_type,
                "account_name": log.account_name,
                "success": log.success,
                "message": log.message,
                "time": log.created_at.isoformat() if log.created_at else None,
            }
            for log in result.scalars().all()
        ]


# ---------------------------------------------------------------------------
# Share sign-in: friends help via browser
# ---------------------------------------------------------------------------

import secrets, string as _string

def _gen_token():
    return ''.join(secrets.choice(_string.ascii_letters + _string.digits) for _ in range(12))

SHARE_CSS = """<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
body{
  font-family:-apple-system,BlinkMacSystemFont,"SF Pro Text","PingFang SC","Microsoft YaHei",sans-serif;
  background:#F8F8F6;color:#16161A;min-height:100vh;min-height:100dvh;
  display:flex;align-items:center;justify-content:center;
  -webkit-font-smoothing:antialiased;
}
.container{width:100%;max-width:340px;padding:0 24px}
.label{font-size:13px;font-weight:500;color:#5B5B66;letter-spacing:.03em;text-transform:uppercase;margin-bottom:6px}
.course{font-size:20px;font-weight:600;color:#16161A;line-height:1.3;margin-bottom:24px;letter-spacing:-.01em}
.btn{
  width:100%;padding:15px 0;border:none;border-radius:10px;
  font-size:16px;font-weight:560;font-family:inherit;
  background:#16161A;color:#FFFFFF;
  cursor:pointer;transition:all .2s ease;margin-bottom:12px;
}
.btn:hover:not(:disabled){background:#2D2D35;transform:scale(1.01)}
.btn:active:not(:disabled){transform:scale(.99)}
.btn:disabled{background:#E4E4E0;color:#5B5B66;cursor:not-allowed}
.btn.done{background:#F0FDF7;color:#10B981;cursor:default}
.btn.outline{background:transparent;color:#5B5B66;border:1px solid #E4E4E0}
.preview{width:100%;border-radius:10px;margin-bottom:12px;display:none;max-height:200px;object-fit:contain;background:#E4E4E0}
.status{font-size:14px;color:#5B5B66;text-align:center;margin-top:12px;min-height:20px;transition:color .3s}
.status.ok{color:#10B981}
.status.err{color:#EF4444}
input[type="file"]{display:none}
.type-hint{font-size:13px;color:#8E8E8E;text-align:center;margin-bottom:16px}
</style>"""

SHARE_SCRIPT_COMMON = """
const token='{token}';
const $=s=>document.getElementById(s);
const st=$('status');
let done=false;

async function uploadImage(file) {
  const fd=new FormData();fd.append('image',file);
  const r=await fetch('/api/share/submit/'+token,{method:'POST',body:fd});
  if(!r.ok){const e=await r.json();throw new Error(e.detail||'上传失败')}
  return r.json();
}

async function submitData(data) {
  st.textContent='提交中...';
  try{
    const fd=new FormData();
    for(const[k,v]of Object.entries(data)) fd.append(k,v);
    const r=await fetch('/api/share/submit/'+token,{method:'POST',body:fd});
    if(!r.ok){const e=await r.json();st.className='status err';st.textContent=e.detail||'提交失败';return false}
    return true;
  }catch(e){st.className='status err';st.textContent='网络错误，请重试';return false}
}

function waitForResult() {
  st.textContent='等待签到...';
  const poll=setInterval(async()=>{
    if(done) return;
    const r=await fetch('/api/share/status/'+token);const d=await r.json();
    if(d.status==='success'){clearInterval(poll);done=true;st.className='status ok';st.textContent='签到成功！'}
    else if(d.status==='failed'){clearInterval(poll);st.className='status err';st.textContent=d.result||'签到失败'}
  },3000);
}
"""

def _make_share_page(course: str, token: str, sign_type: str, body_html: str) -> str:
    """Build full HTML page from shared CSS, common JS, and type-specific body."""
    html = f"""<!DOCTYPE html><html lang="zh"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>帮TA签到</title>{SHARE_CSS}</head><body>
<div class="container">
  <p class="label">{sign_type}签到</p>
  <p class="course">{course}</p>
  {body_html}
  <p class="status" id="status"></p>
</div>
<script>{SHARE_SCRIPT_COMMON}</script></body></html>"""
    return html.replace("{token}", token)


SHARE_PAGE_NORMAL = """<p class="type-hint">点击下方按钮，帮朋友完成签到</p>
<button class="btn" id="btn" onclick="(async()=>{
  const b=$('btn');b.disabled=true;b.textContent='提交中';
  const ok=await submitData({name:'friend'});
  if(ok){b.textContent='等待签到';waitForResult()}
  else{b.disabled=false;b.textContent='重试'}
})()">一键帮签</button>"""

SHARE_PAGE_QR = """<p class="type-hint">请将二维码对准摄像头</p>
<div style="position:relative;width:100%;border-radius:12px;overflow:hidden;background:#000;margin-bottom:12px;aspect-ratio:1">
  <video id="video" autoplay playsinline style="width:100%;height:100%;object-fit:cover"></video>
  <canvas id="canvas" style="display:none"></canvas>
</div>
<button class="btn" id="btn" onclick="startScan()">开启摄像头扫描</button>
<script>
let stream=null,scanning=false,scanTimer=null,useNative=false,lastSubmit=0,submitBusy=false;

async function startScan(){
  const b=$('btn'),v=$('video'),cv=$('canvas');
  if(scanning){stopScan();return}
  try{
    // Try native BarcodeDetector (Chrome 83+)
    try{ if('BarcodeDetector' in window) new BarcodeDetector({formats:['qr_code']}).detect(document.createElement('canvas'));
      useNative=true; }catch(e){ useNative=false; }
    stream=await navigator.mediaDevices.getUserMedia({video:{facingMode:'environment',width:{ideal:640},height:{ideal:640}}});
    v.srcObject=stream;await v.play();
    cv.width=640;cv.height=640;
    b.textContent='扫描中...';scanning=true;
    st.textContent=useNative?'浏览器原生识别':'云端识别模式';
    const ctx=cv.getContext('2d');let tick=0;
    scanTimer=setInterval(async()=>{
      if(!scanning)return;tick++;
      ctx.drawImage(v,0,0,640,640);
      if(useNative){
        try{
          const codes=await(new BarcodeDetector({formats:['qr_code']})).detect(cv);
          if(codes.length>0){onFound(b,cv);return}
        }catch(e){useNative=false;st.textContent='切换到云端识别模式'}
        if(tick>600){stopScan();st.className='status err';st.textContent='超时，请重试';b.disabled=false;b.textContent='重新扫描'}
      }else{
        // Fallback: send frame to server for OpenCV decode every 500ms
        if(tick%4===0&&!submitBusy&&Date.now()-lastSubmit>500){
          submitBusy=true;lastSubmit=Date.now();
          const blob=await new Promise(r=>cv.toBlob(r,'image/jpeg',0.92));
          try{
            const fd=new FormData();fd.append('image',blob,'qr.jpg');
            const r=await fetch('/api/share/submit/'+token,{method:'POST',body:fd});
            const d=await r.json();
            if(d.enc){onFound(b,cv);return}
          }catch(e){}
          submitBusy=false;
        }
        if(tick>240){stopScan();st.className='status err';st.textContent='超时，请确保二维码清晰完整';b.disabled=false;b.textContent='重新扫描'}
      }
    },150);
  }catch(e){st.className='status err';st.textContent='无法打开摄像头：'+e.message;b.disabled=false;b.textContent='重试'}
}

function onFound(b,cv){
  stopScan();b.textContent='等待签到';b.className='btn done';st.className='status ok';st.textContent='二维码已识别';
  waitForResult();
}
function stopScan(){
  scanning=false;if(scanTimer)clearInterval(scanTimer);
  if(stream){stream.getTracks().forEach(t=>t.stop());stream=null}
  const v=$('video');if(v)v.srcObject=null;
}
</script>"""

SHARE_PAGE_LOCATION = """<p class="type-hint">请授权GPS并提交你的位置</p>
<button class="btn" id="btn" onclick="(async()=>{
  const b=$('btn');b.disabled=true;b.textContent='获取位置...';st.textContent='';
  if(!navigator.geolocation){st.className='status err';st.textContent='浏览器不支持GPS';b.disabled=false;return}
  navigator.geolocation.getCurrentPosition(async pos=>{
    st.textContent='提交中...';
    const ok=await submitData({lat:pos.coords.latitude,lon:pos.coords.longitude});
    if(ok){b.textContent='等待签到';b.className='btn done';waitForResult()}
    else{b.disabled=false;b.textContent='重试'}
  },err=>{st.className='status err';st.textContent='GPS获取失败，请允许位置权限';b.disabled=false;b.textContent='重试'},
  {enableHighAccuracy:true,timeout:10000,maximumAge:0});
})()">获取位置并帮签</button>"""

SHARE_PAGE_PHOTO = """<p class="type-hint">请拍照上传签到照片</p>
<img class="preview" id="preview">
<input type="file" id="fileInput" accept="image/*" capture="environment" onchange="(async()=>{
  const f=$('fileInput').files[0];if(!f)return;
  $('preview').src=URL.createObjectURL(f);$('preview').style.display='block';
  const b=$('btn');b.disabled=true;b.textContent='上传中...';
  const fd=new FormData();fd.append('image',f);
  try{
    const r=await fetch('/api/share/submit/'+token,{method:'POST',body:fd});
    if(!r.ok){const e=await r.json();throw new Error(e.detail||'上传失败')}
    b.textContent='等待签到';b.className='btn done';st.textContent='照片已上传';
    waitForResult();
  }catch(e){st.className='status err';st.textContent=e.message;b.disabled=false;b.textContent='重新拍照'}
})()">
<button class="btn" id="btn" onclick="$('fileInput').click()">拍照上传</button>"""

@app.post("/api/share/create")
async def create_share(
    course_name: str = Form(...),
    sign_type: str = Form(...),
    active_id: str = Form(...),
    name: str = Form(""),
    uid: str = Form(""),
    fid: str = Form(""),
    cookies_json: str = Form(""),
    phone: str = Form(""),
    password: str = Form(""),
):
    """App creates a share link with sign-in credentials + login creds for fresh cookies."""
    await _cleanup_expired_shares()  # remove old ones before creating new
    token = _gen_token()
    course_fixed = course_name
    name_fixed = name if name else ""
    try:
        course_fixed = course_name.encode("latin-1").decode("utf-8")
    except (UnicodeDecodeError, UnicodeEncodeError):
        pass
    try:
        name_fixed = name.encode("latin-1").decode("utf-8") if name else ""
    except (UnicodeDecodeError, UnicodeEncodeError):
        pass
    async with async_session() as db:
        st = ShareToken(token=token, course_name=course_fixed, sign_type=sign_type,
                        active_id=active_id, uid=uid, fid=fid, cookies_json=cookies_json,
                        phone=phone, password=password)
        db.add(st)
        await db.commit()
    return {"token": token, "url": f"https://xxt.meng-xun.top/api/share/page/{token}"}


@app.get("/api/share/page/{token}")
async def share_page(token: str):
    """Browser page for friends — type-specific UI. Expires after 5 min."""
    from fastapi.responses import HTMLResponse
    async with async_session() as db:
        result = await db.execute(select(ShareToken).where(ShareToken.token == token))
        st = result.scalar_one_or_none()
        if not st:
            return HTMLResponse("<h2>链接已过期</h2>", status_code=404)
        age = (datetime.datetime.utcnow() - st.created_at).total_seconds() / 60
        if age > SHARE_TTL_MINUTES:
            return HTMLResponse("<h2>链接已过期（5分钟）</h2>", status_code=404)
    course = st.course_name or "课程签到"
    sign_type = st.sign_type or "normal"
    type_labels = {"qr": "二维码", "location": "位置", "photo": "拍照", "normal": "普通"}
    label = type_labels.get(sign_type, sign_type)
    pages = {"qr": SHARE_PAGE_QR, "location": SHARE_PAGE_LOCATION, "photo": SHARE_PAGE_PHOTO}
    body = pages.get(sign_type, SHARE_PAGE_NORMAL)
    html = _make_share_page(course, token, label, body)
    return HTMLResponse(html, media_type="text/html; charset=utf-8")


@app.post("/api/share/submit/{token}")
async def share_submit(token: str, image: UploadFile = File(None),
                        lat: str = Form(""), lon: str = Form(""),
                        name: str = Form("")):
    """Friend submits sign-in data — image for QR/photo, lat/lon for location."""
    async with async_session() as db:
        st_result = await db.execute(select(ShareToken).where(ShareToken.token == token))
        st = st_result.scalar_one_or_none()
        if not st:
            raise HTTPException(404, detail="链接已过期")
        sign_type = st.sign_type or "normal"
        enc = ""
        image_path = ""
        if image and image.filename:
            contents = await image.read()
            import os as _os
            upload_dir = _os.path.join(_os.path.dirname(__file__), "uploads")
            _os.makedirs(upload_dir, exist_ok=True)
            fname = f"{token}_{image.filename}"
            fpath = _os.path.join(upload_dir, fname)
            with open(fpath, "wb") as f:
                f.write(contents)
            if sign_type == "qr":
                try:
                    img = Image.open(io.BytesIO(contents))
                    img_np = np.array(img.convert("RGB"))
                    import cv2
                    detector = cv2.QRCodeDetector()
                    data, _, _ = detector.detectAndDecode(img_np)
                    if data:
                        # Parse URL query params to extract enc (robust against varying lengths)
                        for part in data.replace("?", "&").split("&"):
                            if part.startswith("enc="):
                                enc = part[4:]
                                break
                        if not enc:
                            enc = data  # fallback: raw data
                except Exception:
                    enc = ""
            elif sign_type == "photo":
                image_path = f"/uploads/{fname}"
        import json as _json
        if enc or lat or lon:
            # Find existing QR/location pending, update it instead of creating duplicate
            presult = await db.execute(
                select(PendingSign).where(PendingSign.token == token,
                                          PendingSign.password == sign_type,
                                          PendingSign.status == "pending")
            )
            existing = presult.scalars().first()
            if existing:
                try: data = _json.loads(existing.phone)
                except: data = {}
                if enc: data["enc"] = enc
                if lat: data["lat"] = lat
                if lon: data["lon"] = lon
                existing.phone = _json.dumps(data)
            else:
                extra = {}
                if enc: extra["enc"] = enc
                if lat or lon: extra["lat"] = lat; extra["lon"] = lon
                ps = PendingSign(token=token, phone=_json.dumps(extra),
                                 password=sign_type, name=name or "friend")
                db.add(ps)
            await db.commit()
        # Non-QR/location submissions (empty frames) are silently ignored
    return {"status": "ok", "enc": enc}


@app.get("/api/share/sign-params/{token}")
async def share_sign_params(token: str):
    """Service calls this to get Chaoxing sign-in parameters."""
    import json as _json
    async with async_session() as db:
        st_result = await db.execute(select(ShareToken).where(ShareToken.token == token))
        st = st_result.scalar_one_or_none()
        if not st:
            raise HTTPException(404, detail="not found")
        # Credentials from ShareToken (stored at share creation, never deleted)
        uid = st.uid or ""
        fid = st.fid or "0"
        cookies_str = st.cookies_json or ""
        # Friend's data from PendingSign (enc, lat, lon)
        enc = ""
        lat = ""
        lon = ""
        name = "friend"
        presult = await db.execute(
            select(PendingSign).where(PendingSign.token == token,
                                      PendingSign.status == "pending")
        )
        for pre in presult.scalars().all():
            name = pre.name or name
            try:
                extra = _json.loads(pre.phone)
                if isinstance(extra, dict):
                    if extra.get("enc"): enc = extra["enc"]
                    if extra.get("lat"): lat = extra["lat"]
                    if extra.get("lon"): lon = extra["lon"]
            except Exception:
                pass
        return {
            "active_id": st.active_id,
            "uid": uid, "fid": fid,
            "name": name,
            "cookies_json": cookies_str,
            "phone": st.phone or "",
            "password": st.password or "",
            "enc": enc, "lat": lat, "lon": lon,
        }


@app.get("/api/share/pending/{token}")
async def share_pending(token: str):
    """App polls for pending sign requests — includes enc/lat/lon from friend."""
    import json as _json
    async with async_session() as db:
        result = await db.execute(
            select(PendingSign).where(
                PendingSign.token == token,
                PendingSign.status == "pending"
            )
        )
        items = result.scalars().all()
        out = []
        for p in items:
            entry = {"id": p.id, "name": p.name, "phone": p.phone, "password": p.password}
            # Parse extra data from friend submission (enc, lat, lon, image_path)
            try:
                extra = _json.loads(p.phone)
                if isinstance(extra, dict):
                    if "enc" in extra:
                        entry["enc"] = extra["enc"]
                    if "lat" in extra:
                        entry["lat"] = extra["lat"]
                    if "lon" in extra:
                        entry["lon"] = extra["lon"]
                    if "image_path" in extra:
                        entry["image_path"] = extra["image_path"]
            except Exception:
                pass
            out.append(entry)
        return out


@app.post("/api/share/result")
async def share_result(
    req_id: int = Form(...),
    status: str = Form("success"),
):
    """App reports sign-in result for a pending request."""
    async with async_session() as db:
        ps = await db.get(PendingSign, req_id)
        if ps:
            ps.status = status
            ps.result = "ok" if status == "success" else "fail"
            await db.commit()
    return {"status": "ok"}


@app.get("/api/share/status/{token}")
async def share_status(token: str, phone: str = ""):
    """Friend polls for their sign-in result."""
    async with async_session() as db:
        query = select(PendingSign).where(
            PendingSign.token == token,
            PendingSign.status.in_(["success", "failed"])
        )
        if phone:
            query = query.where(PendingSign.phone == phone)
        result = await db.execute(query.order_by(PendingSign.id.desc()).limit(1))
        ps = result.scalar_one_or_none()
    if ps:
        return {"status": ps.status, "result": ps.result}
    return {"status": "pending", "result": ""}


# ---------------------------------------------------------------------------
# Sign-in operations — all Chaoxing calls now happen on the phone
# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# QR code decoding
# ---------------------------------------------------------------------------

@app.post("/api/sign/decode-qr")
async def decode_qr(image: UploadFile = File(...)):
    """Decode a QR code from an uploaded image using OpenCV."""
    try:
        data = await image.read()
        import cv2
        nparr = np.frombuffer(data, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
        if img is None:
            raise HTTPException(400, detail="无法解析图片")

        detector = cv2.QRCodeDetector()
        qr_text, points, _ = detector.detectAndDecode(img)

        if not qr_text:
            raise HTTPException(400, detail="未检测到二维码，请确保二维码完整清晰")

        print(f"[QR] Decoded: {qr_text[:200]}")

        import re
        enc_match = re.search(r'enc=([A-Za-z0-9]+)', qr_text)
        if enc_match:
            return {"enc": enc_match.group(1), "raw": qr_text}

        return {"enc": qr_text, "raw": qr_text}

    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(400, detail=f"二维码解码失败: {str(e)}")


# ---------------------------------------------------------------------------
# Update check
# ---------------------------------------------------------------------------

@app.get("/api/update/check")
async def check_update():
    """Return current app version info from update.json."""
    import json as _json, os as _os
    config_path = _os.path.join(_os.path.dirname(__file__), "update.json")
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            cfg = _json.load(f)
            cfg["url"] = "https://xxt.meng-xun.top/api/update/download"
            return cfg
    except Exception:
        return {"versionCode": 0, "version": "0", "url": ""}


@app.get("/api/update/download")
async def download_apk():
    """Serve the APK file for auto-update."""
    import os as _os
    from fastapi.responses import FileResponse
    apk_path = _os.path.join(_os.path.dirname(__file__), "mengxun-sign.apk")
    if not _os.path.exists(apk_path):
        raise HTTPException(404, detail="APK not found")
    return FileResponse(apk_path, media_type="application/vnd.android.package-archive",
                        filename="mengxun-sign.apk")


# ---------------------------------------------------------------------------
# Health
# ---------------------------------------------------------------------------

@app.get("/api/health")
async def health():
    return {"status": "ok", "version": "2.0.0"}


# ---------------------------------------------------------------------------
# Entry
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host=HOST, port=PORT, reload=True)
