import os
import shutil
import sys
import tempfile
import zipfile

script_dir = os.path.dirname(os.path.abspath(__file__))
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)
from http_download import download_file

def update_spine():
    current_dir = os.path.dirname(os.path.abspath(__file__))
    target_dir = os.path.join(current_dir, "..", "3rd", "spine", "spine")
    spine_version = "4.2"
    version_file = os.path.join(target_dir, "version.txt")
    
    if os.path.exists(version_file):
        with open(version_file, "r") as f:
            if f.read().strip() == spine_version:
                print(f"Spine {spine_version} is already up to date. Skipping download.")
                return
                
    zip_url = f"https://github.com/EsotericSoftware/spine-runtimes/archive/refs/heads/{spine_version}.zip"
    print(f"Downloading {zip_url} ...")
    
    with tempfile.TemporaryDirectory() as temp_dir:
        zip_path = os.path.join(temp_dir, "spine.zip")
        
        try:
            download_file(zip_url, zip_path, timeout=120)
        except RuntimeError as e:
            print(f"Failed to download: {e}")
            sys.exit(1)
            
        print("Extracting...")
        try:
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                zip_ref.extractall(temp_dir)
        except Exception as e:
            print(f"Failed to extract: {e}")
            sys.exit(1)
            
        extracted_folder = os.path.join(temp_dir, "spine-runtimes-4.2", "spine-cpp", "spine-cpp")
        
        if not os.path.exists(extracted_folder):
            print("Error: Could not find spine-cpp in the extracted zip.")
            sys.exit(1)

        print("Updating files...")
        if os.path.exists(target_dir):
            shutil.rmtree(target_dir)
        os.makedirs(target_dir)
        
        # Copy include/spine/*.h
        include_dir = os.path.join(extracted_folder, "include", "spine")
        for file in os.listdir(include_dir):
            if file.endswith(".h") or file.endswith(".cpp"):
                shutil.copy(os.path.join(include_dir, file), os.path.join(target_dir, file))
                
        # Copy src/spine/*.cpp
        src_dir = os.path.join(extracted_folder, "src", "spine")
        for file in os.listdir(src_dir):
            if file.endswith(".h") or file.endswith(".cpp"):
                shutil.copy(os.path.join(src_dir, file), os.path.join(target_dir, file))
                
        print("Cleaning up temporary files and deleting archive...")
        
    # Save version
    with open(version_file, "w") as f:
        f.write(spine_version)

    print(f"Spine-cpp updated successfully to version {spine_version} in 3rd/spine/spine!")

if __name__ == "__main__":
    update_spine()
