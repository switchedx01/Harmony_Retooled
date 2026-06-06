#!/usr/bin/env python3
import json
import urllib.request
import urllib.error
import sys
import os
import tarfile
import tempfile
import shutil
import stat

REPO = "switchedx01/Harmony_Retooled"
API_URL = f"https://api.github.com/repos/{REPO}/releases"

def install_binary(src_bin):
    print("Installing update...", flush=True)
    dest_bin = "./harmony_player"
    new_bin = "./harmony_player.new"
    try:
        shutil.copy2(src_bin, new_bin)
        os.chmod(new_bin, os.stat(new_bin).st_mode | stat.S_IEXEC)
        os.replace(new_bin, dest_bin)
        print("Update successful! Restart app.", flush=True)
    except Exception as e:
        print(f"Install failed: {e}", flush=True)
        if os.path.exists(new_bin):
            os.remove(new_bin)
        raise

def main():
    # Get current version from arguments
    current_version = sys.argv[1] if len(sys.argv) > 1 else ""
    # Strip any prefix like 'v' or 'Harmony ' for clean comparison if needed, 
    # but exact mismatch/difference check works well.
    current_version_clean = current_version.lower().replace("harmony ", "").strip()

    print("Checking for updates...", flush=True)
    try:
        req = urllib.request.Request(API_URL, headers={'User-Agent': 'Harmony-Updater'})
        with urllib.request.urlopen(req) as response:
            releases = json.loads(response.read().decode('utf-8'))
            if not releases or not isinstance(releases, list):
                print("No releases found on GitHub.", flush=True)
                return
            
            data = releases[0]
            tag_name = data.get('name', data.get('tag_name', 'Unknown'))
            print(f"Latest release: {tag_name}", flush=True)
            
            tag_name_clean = tag_name.lower().replace("harmony ", "").strip()
            
            if current_version_clean and current_version_clean == tag_name_clean:
                print("Already up to date.", flush=True)
                return

            download_url = None
            asset_name = None
            is_source_tarball = False

            assets = data.get('assets', [])
            for asset in assets:
                name = asset['name']
                if name.endswith('.tar.gz') or name == 'harmony_player':
                    download_url = asset['browser_download_url']
                    asset_name = name
                    break
            
            # Fallback to source code if no assets found
            if not download_url:
                download_url = data.get('tarball_url')
                if download_url:
                    print("No prebuilt assets, downloading source code...", flush=True)
                    asset_name = "source.tar.gz"
                    is_source_tarball = True
                else:
                    print("No suitable asset or source found.", flush=True)
                    return
            else:
                print(f"Downloading {asset_name}...", flush=True)

            temp_dir = tempfile.mkdtemp()
            download_path = os.path.join(temp_dir, asset_name)
            
            # Github's tarball_url redirects, so we need a Request with headers to follow it
            req_dl = urllib.request.Request(download_url, headers={'User-Agent': 'Harmony-Updater'})
            with urllib.request.urlopen(req_dl) as response, open(download_path, 'wb') as out_file:
                shutil.copyfileobj(response, out_file)
            
            if asset_name.endswith('.tar.gz'):
                print("Extracting...", flush=True)
                with tarfile.open(download_path, 'r:gz') as tar:
                    tar.extractall(path=temp_dir)
                
                if is_source_tarball:
                    # Find the extracted repository folder
                    repo_dir = None
                    for item in os.listdir(temp_dir):
                        item_path = os.path.join(temp_dir, item)
                        if os.path.isdir(item_path):
                            repo_dir = item_path
                            break
                    
                    if repo_dir:
                        print("Building from source...", flush=True)
                        import subprocess
                        # Run make inside the repo directory
                        result = subprocess.run(['make'], cwd=repo_dir, capture_output=True, text=True)
                        if result.returncode == 0:
                            built_bin = os.path.join(repo_dir, 'harmony_player')
                            if os.path.exists(built_bin):
                                install_binary(built_bin)
                            else:
                                print("Build successful but harmony_player not found.", flush=True)
                        else:
                            print(f"Build failed: {result.stderr[:50]}...", flush=True)
                    else:
                        print("Failed to find extracted source folder.", flush=True)

                else: # Existing logic for pre-built tar.gz
                    extracted_bin = None
                    for root, dirs, files in os.walk(temp_dir):
                        if 'harmony_player' in files:
                            extracted_bin = os.path.join(root, 'harmony_player')
                            break
                            
                    if extracted_bin:
                        install_binary(extracted_bin)
                    else:
                        print("Error: harmony_player not in archive.", flush=True)
            else:
                install_binary(download_path)

    except urllib.error.HTTPError as e:
        if e.code == 404:
            print("No releases found on GitHub yet.", flush=True)
        else:
            print(f"HTTP Error {e.code}", flush=True)
    except Exception as e:
        print(f"Failed: {str(e)[:30]}", flush=True)

if __name__ == "__main__":
    main()
