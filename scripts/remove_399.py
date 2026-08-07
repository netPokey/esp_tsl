import re
import sys

def main():
    filepath = 'src/can_b_replay.cpp'
    with open(filepath, 'r') as f:
        lines = f.readlines()

    new_lines = []
    removed_count = 0
    for line in lines:
        if re.search(r'\{0x[0-9A-Fa-f]+,\s*\d+,', line):
            if re.search(r'\{0x3E9,\s*\d+,', line, re.IGNORECASE):
                new_lines.append(line)
            else:
                removed_count += 1
        else:
            new_lines.append(line)

    content = ''.join(new_lines)

    # Update REPLAY_FRAME_COUNT
    def update_count(match):
        old_count = int(match.group(1))
        new_count = old_count - removed_count
        return f'REPLAY_FRAME_COUNT = {new_count};'

    content = re.sub(r'REPLAY_FRAME_COUNT = (\d+);', update_count, content)

    with open(filepath, 'w') as f:
        f.write(content)

    print(f"Removed {removed_count} frames (kept only 0x3E9). Updated REPLAY_FRAME_COUNT.")

if __name__ == '__main__':
    main()
