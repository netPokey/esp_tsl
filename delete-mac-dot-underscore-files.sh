#!/bin/bash
set -euo pipefail

TARGET_DIR="./"

if [ ! -d "$TARGET_DIR" ]; then
    echo "目录不存在: $TARGET_DIR" >&2
    exit 1
fi

echo "清理目录: $TARGET_DIR"

deleted_count=0
skipped_count=0
while IFS= read -r -d '' file; do
    echo "删除: $file"
    if rm -f "$file" 2>/dev/null; then
        deleted_count=$((deleted_count + 1))
    else
        echo "跳过（删除失败）: $file" >&2
        skipped_count=$((skipped_count + 1))
    fi
done < <(find "$TARGET_DIR" -type f -name '._*' -print0)

echo "完成，共删除 $deleted_count 个 ._ 文件，跳过 $skipped_count 个"