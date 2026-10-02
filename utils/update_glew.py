import os
import shutil
import sys
import zipfile

script_dir = os.path.dirname(os.path.abspath(__file__))
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)
from http_download import download_file

def download_and_extract_glew():
    glew_version = "2.3.1"
    url = f"https://github.com/nigels-com/glew/releases/download/glew-{glew_version}/glew-{glew_version}-win32.zip"
    zip_path = "glew.zip"
    target_dir = "glew"
    version_file = os.path.join(target_dir, "version.txt")

    if os.path.exists(version_file):
        with open(version_file, "r") as f:
            if f.read().strip() == glew_version:
                print(f"GLEW {glew_version} is already up to date. Skipping download.")
                return

    print(f"Downloading GLEW {glew_version}...")
    download_file(url, zip_path)

    print("Extracting GLEW...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(".")

    extracted_folder = f"glew-{glew_version}"
    
    # Remove old glew folder if it exists
    if os.path.exists(target_dir):
        shutil.rmtree(target_dir)
        
    # Rename extracted folder to 'glew'
    os.rename(extracted_folder, target_dir)
    
    # Save version
    with open(version_file, "w") as f:
        f.write(glew_version)
    
    # Cleanup zip
    os.remove(zip_path)
    print("GLEW updated successfully.")

if __name__ == "__main__":
    # Change working directory to 3rd/
    script_dir = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else os.getcwd()
    os.chdir(os.path.join(script_dir, "..", "3rd"))
    
    download_and_extract_glew()
    print("Done!")
