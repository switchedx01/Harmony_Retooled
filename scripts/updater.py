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
API_URL = f"https://api.github.com/repos/{REPO}/releases/latest"

def main():
    print("Checking for updates...", flush=True)
    try:
        req = urllib.request.Request(API_URL, headers={'User-Agent': 'Harmony-Updater'})
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read().decode('utf-8'))
            
            tag_name = data.get('tag_name', 'Unknown')
            print(f"Found latest release: {tag_name}", flush=True)
            
            assets = data.get('assets', [])
            if not assets:
                print("No assets found in release.", flush=True)
                return

            download_url = None
            asset_name = None
            for asset in assets:
                name = asset['name']
                if name.endswith('.tar.gz') or name == 'harmony_player':
                    download_url = asset['browser_download_url']
                    asset_name = name
                    break
            
            if not download_url:
                print("No suitable asset found.", flush=True)
                return
            
            print(f"Downloading {asset_name}...", flush=True)
            temp_dir = tempfile.mkdtemp()
            download_path = os.path.join(temp_dir, asset_name)
            
            urllib.request.urlretrieve(download_url, download_path)
            
            if asset_name.endswith('.tar.gz'):
                print("Extracting...", flush=True)
                with tarfile.open(download_path, 'r:gz') as tar:
                    tar.extractall(path=temp_dir)
                
                extracted_bin = None
                for root, dirs, files in os.walk(temp_dir):
                    if 'harmony_player' in files:
                        extracted_bin = os.path.join(root, 'harmony_player')
                        break
                        
                if extracted_bin:
                    print("Installing update...", flush=True)
                    shutil.move(extracted_bin, "./harmony_player")
                    os.chmod("./harmony_player", os.stat("./harmony_player").st_mode | stat.S_IEXEC)
                    print("Update successful! Restart app.", flush=True)
                else:
                    print("Error: harmony_player not in archive.", flush=True)
            else:
                print("Installing update...", flush=True)
                shutil.move(download_path, "./harmony_player")
                os.chmod("./harmony_player", os.stat("./harmony_player").st_mode | stat.S_IEXEC)
                print("Update successful! Restart app.", flush=True)

    except urllib.error.HTTPError as e:
        if e.code == 404:
            print("No releases found on GitHub yet.", flush=True)
        else:
            print(f"HTTP Error {e.code}", flush=True)
    except Exception as e:
        print(f"Failed: {str(e)[:30]}", flush=True)

if __name__ == "__main__":
    main()
