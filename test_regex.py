import re

with open('src/can_b_replay.cpp', 'r') as f:
    content = f.read()

pattern = r'(static const ReplayFrame REPLAY_FRAMES\[\] PROGMEM = \{).*?(\n\};)'
match = re.search(pattern, content, flags=re.DOTALL)
if match:
    print("Match found!")
else:
    print("Match failed!")
