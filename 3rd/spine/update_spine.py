import os
import shutil
import urllib.request
import zipfile
import tempfile
import sys

def update_spine():
    current_dir = os.path.dirname(os.path.abspath(__file__))
    target_dir = os.path.join(current_dir, "spine")
    
    zip_url = "https://github.com/EsotericSoftware/spine-runtimes/archive/refs/heads/4.2.zip"
    print(f"Downloading {zip_url} ...")
    
    with tempfile.TemporaryDirectory() as temp_dir:
        zip_path = os.path.join(temp_dir, "spine.zip")
        
        try:
            # Download with a timeout to prevent hanging
            req = urllib.request.Request(zip_url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=30) as response, open(zip_path, 'wb') as out_file:
                shutil.copyfileobj(response, out_file)
        except Exception as e:
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
        
    print("Spine-cpp updated successfully in 3rd/spine/spine!")

if __name__ == "__main__":
    update_spine()
