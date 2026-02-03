import os
import subprocess
import sys


def copy_emulator_bin():
    old_filename = './build/emulator.bin'
    new_filename = '../../../server/image/emulator.bin'

    if os.path.exists(old_filename):
        subprocess.run(['cp', '-f', old_filename, new_filename], check=True)
    else:
        print(f'{old_filename} does not exist.')

def copy_spiffs_bin():
    old_filename = './build/spiffs.bin'
    new_filename = '../../../server/image/emulator.spiffs.bin'

    if os.path.exists(old_filename):
        subprocess.run(['cp', '-f', old_filename, new_filename], check=True)
    else:
        print(f'{old_filename} does not exist.')

def copy_index_html():
    old_filename = './spiffs/index.html'
    new_filename = '../../../server/image/index.html'

    if os.path.exists(old_filename):
        subprocess.run(['cp', '-f', old_filename, new_filename], check=True)
    else:
        print(f'{old_filename} does not exist.')

# 主程式，執行所有步驟
def main():
    copy_emulator_bin()  # 複製命名 emulator.bin
    copy_spiffs_bin()    # 複製命名 emulator.spiffs.bin
    copy_index_html()    # 複製 index.html

if __name__ == '__main__':
    # 從命令行參數獲取版本號
    main()
