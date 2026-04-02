#!/data/data/com.termux/files/usr/bin/bash
# setup_and_push.sh
# Jalankan sekali di Termux untuk setup repo dan push ke GitHub
# Usage: bash setup_and_push.sh

set -e  # stop jika ada error

REPO_URL="https://github.com/brruham-arch/libvoicefx.git"
WORK_DIR="$HOME/libvoicefx"

echo "========================================"
echo "  libvoicefx - Setup & Push Script"
echo "========================================"

# ── 1. Install dependencies ────────────────
echo ""
echo "[1/5] Install git & zip..."
pkg install -y git zip unzip 2>/dev/null || true

# ── 2. Clone atau init repo ────────────────
echo ""
echo "[2/5] Setup repo..."
if [ -d "$WORK_DIR/.git" ]; then
    echo "Repo sudah ada, pull latest..."
    cd "$WORK_DIR"
    git pull origin main 2>/dev/null || git pull origin master 2>/dev/null || true
else
    echo "Clone repo..."
    git clone "$REPO_URL" "$WORK_DIR" 2>/dev/null || {
        echo "Repo kosong/baru, init manual..."
        mkdir -p "$WORK_DIR"
        cd "$WORK_DIR"
        git init
        git remote add origin "$REPO_URL"
    }
    cd "$WORK_DIR"
fi

# ── 3. Buat struktur folder ────────────────
echo ""
echo "[3/5] Buat struktur folder..."
mkdir -p jni lua .github/workflows

# ── 4. Download file dari gist / tulis langsung ─────────
echo ""
echo "[4/5] Tulis file sumber..."

# Cek apakah file sudah ada (dari clone)
if [ ! -f "jni/voicefx.c" ]; then
    echo "PERHATIAN: File jni/voicefx.c tidak ada!"
    echo "Pastikan kamu sudah copy file dari Claude ke folder ini."
    echo "Path: $WORK_DIR/jni/voicefx.c"
    echo ""
    echo "Atau jalankan script ini setelah git clone berhasil dan file ada."
    exit 1
fi

echo "File OK:"
ls -la jni/
ls -la lua/ 2>/dev/null || true
ls -la .github/workflows/ 2>/dev/null || true

# ── 5. Git config & push ───────────────────
echo ""
echo "[5/5] Push ke GitHub..."

# Set identity kalau belum ada
git config user.email "brruham-arch@users.noreply.github.com" 2>/dev/null || true
git config user.name "brruham-arch" 2>/dev/null || true

git add -A
git status

# Commit hanya kalau ada perubahan
if git diff --cached --quiet; then
    echo "Tidak ada perubahan baru untuk di-commit."
else
    git commit -m "update: voicefx engine + lua controller"
fi

echo ""
echo "Push ke GitHub..."
echo "(Masukkan username & Personal Access Token kalau diminta)"
echo ""
git push -u origin main 2>/dev/null || git push -u origin master 2>/dev/null

echo ""
echo "========================================"
echo "  DONE! Cek GitHub Actions:"
echo "  https://github.com/brruham-arch/libvoicefx/actions"
echo "========================================"
