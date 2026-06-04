#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_ENV="can"
ENV_NAME="$DEFAULT_ENV"
DO_CLEAN=0
DO_UPLOAD=0
DO_MONITOR=0
DO_MERGE=0
MERGE_OUTPUT=""
PIO_CMD=()

usage() {
    cat <<'USAGE'
用法: ./compile.sh [选项]

选项:
  -e, --env <名称>       指定 PlatformIO 编译环境，默认: can
  -c, --clean            编译前先清理构建产物
  -u, --upload           编译完成后烧录到设备
  -m, --monitor          编译或烧录后打开串口监视器
      --merge            合并 bootloader、分区表和应用，生成 0x0 起始的完整固件
  -o, --output <文件>    指定 --merge 生成的固件文件路径
  -h, --help             显示此帮助信息

示例:
  ./compile.sh
  ./compile.sh --clean --merge
  ./compile.sh --upload --monitor
USAGE
}

fail() {
    echo "错误: $*" >&2
    exit 1
}

resolve_platformio() {
    if command -v pio >/dev/null 2>&1; then
        PIO_CMD=(pio)
        return
    fi

    if command -v platformio >/dev/null 2>&1; then
        PIO_CMD=(platformio)
        return
    fi

    fail "未找到 PlatformIO，请先安装或确认 pio/platformio 在 PATH 中"
}

run_pio() {
    echo "+ ${PIO_CMD[*]} $*"
    "${PIO_CMD[@]}" "$@"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -e|--env)
            [ "$#" -ge 2 ] || fail "$1 需要环境名"
            ENV_NAME="$2"
            shift 2
            ;;
        -c|--clean)
            DO_CLEAN=1
            shift
            ;;
        -u|--upload)
            DO_UPLOAD=1
            shift
            ;;
        -m|--monitor)
            DO_MONITOR=1
            shift
            ;;
        --merge)
            DO_MERGE=1
            shift
            ;;
        -o|--output)
            [ "$#" -ge 2 ] || fail "$1 需要输出文件路径"
            MERGE_OUTPUT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "未知参数: $1"
            ;;
    esac
done

cd "$PROJECT_DIR"
resolve_platformio

echo "项目目录: $PROJECT_DIR"
echo "编译环境: $ENV_NAME"

if [ "$DO_CLEAN" -eq 1 ]; then
    echo "开始清理构建产物..."
    run_pio run -e "$ENV_NAME" -t clean
fi

echo "开始编译固件..."
run_pio run -e "$ENV_NAME"

if [ "$DO_MERGE" -eq 1 ]; then
    MERGE_ARGS=(-e "$ENV_NAME")
    if [ -n "$MERGE_OUTPUT" ]; then
        MERGE_ARGS+=(-o "$MERGE_OUTPUT")
    fi

    echo "开始合并完整固件..."
    echo "+ python3 platformio_merge_firmware.py ${MERGE_ARGS[*]}"
    python3 platformio_merge_firmware.py "${MERGE_ARGS[@]}"
fi

if [ "$DO_UPLOAD" -eq 1 ]; then
    echo "开始烧录固件..."
    run_pio run -e "$ENV_NAME" -t upload
fi

if [ "$DO_MONITOR" -eq 1 ]; then
    echo "打开串口监视器..."
    run_pio device monitor
fi