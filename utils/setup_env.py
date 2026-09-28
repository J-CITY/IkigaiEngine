import os
import sys
import subprocess

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.abspath(os.path.join(script_dir, ".."))
    
    print("Updating git submodules...")
    subprocess.run(["git", "submodule", "update", "--init", "--recursive"], cwd=root_dir, check=True)
    
    print("\nUpdating GLEW (update_deps.py)...")
    subprocess.run([sys.executable, os.path.join(script_dir, "update_deps.py")], cwd=root_dir, check=True)
    
    print("\nEnvironment setup complete!")

if __name__ == "__main__":
    main()
