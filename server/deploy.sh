#!/bin/bash
# 梦寻签到 — 服务器部署脚本
# 使用方法: chmod +x deploy.sh && sudo ./deploy.sh

set -e

APP_DIR="/opt/xx-sign"
VENV_DIR="$APP_DIR/venv"
PYTHON="python3"

echo "========================================"
echo "  梦寻签到服务器部署"
echo "========================================"

# 1. 安装系统依赖
echo "[1/6] 安装系统依赖..."
if command -v apt &>/dev/null; then
    sudo apt update -qq
    sudo apt install -y $PYTHON $PYTHON-venv $PYTHON-pip libzbar0
elif command -v yum &>/dev/null; then
    sudo yum install -y $PYTHON $PYTHON-pip zbar
else
    echo "请手动安装: python3, python3-pip, libzbar0"
fi

# 2. 创建目录
echo "[2/6] 创建应用目录..."
sudo mkdir -p $APP_DIR
sudo cp -r ./*.py $APP_DIR/
sudo cp -r ./routers $APP_DIR/ 2>/dev/null || true
sudo cp requirements.txt $APP_DIR/

# 3. 创建虚拟环境
echo "[3/6] 创建 Python 虚拟环境..."
sudo $PYTHON -m venv $VENV_DIR
sudo $VENV_DIR/bin/pip install --upgrade pip -q
sudo $VENV_DIR/bin/pip install -r $APP_DIR/requirements.txt -q

# 4. 环境配置
echo "[4/6] 配置环境..."
if [ ! -f "$APP_DIR/.env" ]; then
    KEY=$(openssl rand -hex 16 2>/dev/null || python3 -c "import secrets; print(secrets.token_hex(16))")
    cat > /tmp/xx-env << EOF
CREDENTIAL_ENCRYPTION_KEY=$KEY
PORT=8001
API_KEY=
EOF
    sudo mv /tmp/xx-env $APP_DIR/.env
    echo "  已生成 .env (加密密钥: $KEY)"
else
    echo "  .env 已存在，跳过"
fi

# 设置权限
sudo chown -R www-data:www-data $APP_DIR 2>/dev/null || sudo chown -R $USER:$USER $APP_DIR

# 5. 安装 systemd 服务
echo "[5/6] 安装 systemd 服务..."
sudo cp xx-sign.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable xx-sign
sudo systemctl restart xx-sign

# 6. 检查状态
echo "[6/6] 检查服务状态..."
sleep 2
sudo systemctl status xx-sign --no-pager -l
echo ""
echo "========================================"
echo "  部署完成！"
echo "========================================"
echo ""
echo "  服务地址: http://$(curl -s ifconfig.me 2>/dev/null || echo 'YOUR_IP'):8001"
echo "  健康检查: http://YOUR_IP:8001/api/health"
echo ""
echo "  管理命令:"
echo "    sudo systemctl status xx-sign   查看状态"
echo "    sudo systemctl restart xx-sign  重启"
echo "    sudo journalctl -u xx-sign -f   查看日志"
echo ""
echo "  ⚠️  请在云服务器安全组中放行 8001 端口"
echo "  ⚠️  App 端需修改「设置」中的服务器地址为: http://YOUR_IP:8001"
