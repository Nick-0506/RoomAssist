import os
import subprocess
import sys

# 步驟 1: 修改 CMakeLists.txt 文件中的版本號
def update_cmakelists(version):
    cmakelists_path = 'CMakeLists.txt'

    # 讀取 CMakeLists.txt 文件
    with open(cmakelists_path, 'r') as file:
        lines = file.readlines()

    # 更新 BUILD_VERSION 的版本
    with open(cmakelists_path, 'w') as file:
        for line in lines:
            if 'set(BUILD_VERSION' in line:
                line = f'set(BUILD_VERSION "{version}")\n'
            file.write(line)

    print(f'Updated CMakeLists.txt with version {version}')

# 步驟 2: 執行 idf.py
def run_idf_py():
    try:
        subprocess.run(['idf.py', 'build'], check=True)
        print('Executed idf.py build successfully.')
    except subprocess.CalledProcessError as e:
        print(f'Error running idf.py: {e}')

# 步驟 3: 重新命名 emulator.bin
def rename_emulator_bin(version):
    old_filename = './build/emulator.bin'
    new_filename = f'./build/emulator-{version}.bin'

    if os.path.exists(old_filename):
        os.rename(old_filename, new_filename)
        print(f'Renamed {old_filename} to {new_filename}')
    else:
        print(f'{old_filename} does not exist.')

def rename_spiffs_bin(version):
    old_filename = './build/spiffs.bin'
    new_filename = f'./build/emulator-{version}.spiffs.bin'

    if os.path.exists(old_filename):
        os.rename(old_filename, new_filename)
        print(f'Renamed {old_filename} to {new_filename}')
    else:
        print(f'{old_filename} does not exist.')

def copy_versioned_index_html(version):
    src = './spiffs/index.html'
    dst = f'./build/index-{version}.html'
    if os.path.exists(src):
        subprocess.run(['cp', '-f', src, dst], check=True)
        print(f'Copied {src} to {dst}')
    else:
        print(f'{src} does not exist.')

# 步驟 4: 傳檔到伺服器（示例：複製到 ../../../server/image）
def copy_assets_to_server(version):
    server_dir = '../../../server/image'
    os.makedirs(server_dir, exist_ok=True)

    emulator_bin = f'./build/emulator-{version}.bin'
    spiffs_bin = f'./build/emulator-{version}.spiffs.bin'
    index_html = f'./build/index-{version}.html'

    files = [
        (emulator_bin, os.path.join(server_dir, f'emulator-{version}.bin')),
        (spiffs_bin, os.path.join(server_dir, f'emulator-{version}.spiffs.bin')),
        (index_html, os.path.join(server_dir, f'index-{version}.html')),
    ]

    for src, dst in files:
        if os.path.exists(src):
            subprocess.run(['cp', '-f', src, dst], check=True)
            print(f'Copied {src} to {dst}')
        else:
            print(f'{src} does not exist.')

# 主程式，執行所有步驟
def main(version):
    update_cmakelists(version)  # 修改 CMakeLists.txt
    run_idf_py()  # 執行 idf.py
    rename_emulator_bin(version)  # 重新命名 emulator.bin
    rename_spiffs_bin(version)    # 重新命名 spiffs.bin
    copy_versioned_index_html(version)  # 複製 index.html 為版本命名
    copy_assets_to_server(version)      # 複製檔案到伺服器

if __name__ == '__main__':
    # 從命令行參數獲取版本號
    if len(sys.argv) != 2:
        print("Usage: python release.py <version>")
        sys.exit(1)

    version = sys.argv[1]
    main(version)
