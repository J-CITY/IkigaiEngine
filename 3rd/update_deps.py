import urllib.request
import zipfile
import os
import shutil
import subprocess

def download_and_extract_glew():
    glew_version = "2.3.1"
    url = f"https://github.com/nigels-com/glew/releases/download/glew-{glew_version}/glew-{glew_version}-win32.zip"
    zip_path = "glew.zip"
    target_dir = "glew"

    print(f"Downloading GLEW {glew_version}...")
    urllib.request.urlretrieve(url, zip_path)

    print("Extracting GLEW...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(".")

    extracted_folder = f"glew-{glew_version}"
    
    # Remove old glew folder if it exists
    if os.path.exists(target_dir):
        shutil.rmtree(target_dir)
        
    # Rename extracted folder to 'glew'
    os.rename(extracted_folder, target_dir)
    
    # Cleanup zip
    os.remove(zip_path)
    print("GLEW updated successfully.")

if __name__ == "__main__":
    # Change working directory to 3rd/
    script_dir = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else os.getcwd()
    os.chdir(script_dir)
    
    download_and_extract_glew()
    print("Done!")
