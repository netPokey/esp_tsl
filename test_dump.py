import json

target_ids = [0x3E9,0x405,
    0x082, 0x0A9, 0x102, 0x103, 0x118, 0x129, 0x132, 0x145,
    0x189, 0x1F9, 0x20C, 0x21C, 0x229, 0x238, 0x243, 0x249,
    0x257, 0x25A, 0x25D, 0x266, 0x273, 0x292, 0x293, 0x2B4,
    0x2B6, 0x2E1, 0x2E5, 0x2F3, 0x31F, 0x321, 0x332, 0x333,
    0x334, 0x339, 0x33A, 0x352, 0x370, 0x37A,  0x39B,
    0x39D, 0x3A1, 0x3B3, 0x3B6, 0x3C2, 0x3C3, 0x3D2, 0x3D8,
    0x3DF, 0x3E2, 0x3E3,  0x3EA, 0x3F5, 0x3FD, 0x3FE,
    0x401,  0x498, 0x4E2, 0x4E3, 0x4F3, 0x678, 0x679,
    0x68C,
    0x399, 0x7FF
]

frames = []
with open('data/can_batches.ndjson', 'r') as f:
    for line in f:
        if not line.strip(): continue
        data = json.loads(line)
        for frame in data.get('payload', {}).get('FRAMES', []):
            if frame['ID'] in target_ids:
                dlc = frame['DLC']
                data_hex = frame['DATA']
                bytes_list = data_hex.split()
                if len(bytes_list) > 8:
                    bytes_list = bytes_list[:8]
                bytes_str = ", ".join(f"0x{b}" for b in bytes_list)
                if len(bytes_list) < 8:
                    bytes_str += ", " + ", ".join(["0x00"] * (8 - len(bytes_list)))
                bytes_str = bytes_str.strip(', ')
                s = f"    {{0x{frame['ID']:03X}, {dlc}, {{{bytes_str}}}}}"
                frames.append(s)

def calculate_tesla_checksum(msg_id, data_bytes):
    checksum = (msg_id & 0xFF) + ((msg_id >> 8) & 0xFF)
    for b in data_bytes:
        checksum = (checksum + b) & 0xFF
    return checksum

counter_3e9 = 0
for i in range(len(frames)):
    if "0x3E9" in frames[i]:
        byte6 = (counter_3e9 % 16) << 4
        data_bytes = [0x01, 0x88, 0x02, 0x21, 0x00, 0x00, byte6]
        csum = calculate_tesla_checksum(0x3E9, data_bytes)
        old = frames[i]
        frames[i] = f"    {{0x3E9, 8, {{0x01, 0x88, 0x02, 0x21, 0x00, 0x00, 0x{byte6:02X}, 0x{csum:02X}}}}}"
        print(f"Replaced {old} with {frames[i]}")
        counter_3e9 += 1
