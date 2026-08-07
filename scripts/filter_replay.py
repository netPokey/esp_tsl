import json
import re
import os

target_ids = [
    0x082, 0x0A9, 0x102, 0x103, 0x118, 0x129, 0x132, 0x145,
    0x189, 0x1F9, 0x20C, 0x21C, 0x229, 0x238, 0x243, 0x249,
    0x257, 0x25A, 0x25D, 0x266, 0x273, 0x292, 0x293, 0x2B4,
    0x2B6, 0x2E1, 0x2E5, 0x2F3, 0x31F, 0x321, 0x332, 0x333,
    0x334, 0x339, 0x33A, 0x352, 0x370, 0x37A, 0x399, 0x39B,
    0x39D, 0x3A1, 0x3B3, 0x3B6, 0x3C2, 0x3C3, 0x3D2, 0x3D8,
    0x3DF, 0x3E2, 0x3E3, 0x3E9, 0x3EA, 0x3F5, 0x3FD, 0x3FE,
    0x401, 0x405, 0x498, 0x4E2, 0x4E3, 0x4F3, 0x678, 0x679,
    0x68C, 0x7FF
]

# Read ndjson and extract matching frames
frames = []
with open('data/can_batches.ndjson', 'r') as f:
    for line in f:
        if not line.strip(): continue
        data = json.loads(line)
        for frame in data.get('payload', {}).get('FRAMES', []):
            if frame['ID'] in target_ids:
                # convert "DATA": "XX XX" to {0xXX, 0xXX}
                dlc = frame['DLC']
                data_hex = frame['DATA']
                bytes_list = data_hex.split()
                if len(bytes_list) > 8:
                    bytes_list = bytes_list[:8]
                bytes_str = ", ".join(f"0x{b}" for b in bytes_list)
                if len(bytes_list) < 8:
                    bytes_str += ", " + ", ".join(["0x00"] * (8 - len(bytes_list)))
                bytes_str = bytes_str.strip(', ')
                frames.append(f"    {{0x{frame['ID']:03X}, {dlc}, {{{bytes_str}}}}}")

# Now read can_b_replay.cpp
with open('src/can_b_replay.cpp', 'r') as f:
    content = f.read()

# Replace REPLAY_FRAME_COUNT
content = re.sub(
    r'static constexpr uint32_t REPLAY_FRAME_COUNT = \d+;',
    f'static constexpr uint32_t REPLAY_FRAME_COUNT = {len(frames)};',
    content
)

# Replace REPLAY_FRAMES array
pattern = r'(static const ReplayFrame REPLAY_FRAMES\[\] PROGMEM = \{).*?(\n\};)'
replacement = r'\g<1>\n' + ',\n'.join(frames) + r'\g<2>'
content = re.sub(pattern, replacement, content, flags=re.DOTALL)

# Replace lastReplayMs with lastReplayUs
content = content.replace('uint32_t lastReplayMs = 0;', 'uint32_t lastReplayUs = 0;')
content = content.replace('lastReplayMs = millis();', 'lastReplayUs = micros();')
content = content.replace('millis() - lastReplayMs < 5', 'micros() - lastReplayUs < 500')

# Also need to update the update statement which I don't see in the view. Let me check the full sendNextReplayFrame
# Wait, I didn't see the update statement in the view. Let's just do a regex for sendNextReplayFrame body:

# Let's write the modified content
with open('src/can_b_replay.cpp', 'w') as f:
    f.write(content)

print(f"Updated REPLAY_FRAME_COUNT to {len(frames)} and array.")
