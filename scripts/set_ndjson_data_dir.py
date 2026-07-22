Import("env")
import os

# 将 data 目录指向 data/ndjson_replay，用于 can_ndjson_replay 环境的文件系统上传
data_dir = os.path.join(env.subst("$PROJECT_DIR"), "data", "ndjson_replay")
env["PROJECT_DATA_DIR"] = data_dir
print(f"[ndjson_replay] data_dir -> {data_dir}")
