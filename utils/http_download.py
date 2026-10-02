import os
import shutil
import ssl
import subprocess
import sys
import urllib.error
import urllib.request


def _urllib_ssl_context():
    ctx = ssl.create_default_context()
    try:
        import certifi

        ctx.load_verify_locations(cafile=certifi.where())
    except ImportError:
        pass
    return ctx


def _download_with_curl(url, dest_path, timeout):
    curl = "curl.exe" if sys.platform == "win32" else "curl"
    if shutil.which(curl) is None:
        raise FileNotFoundError(curl)
    connect_timeout = str(max(1, min(int(timeout), 60)))
    subprocess.run(
        [curl, "-fsSL", "--connect-timeout", connect_timeout, "-o", dest_path, url],
        check=True,
    )


def download_file(url, dest_path, timeout=120):
    """Download url to dest_path. Uses certifi CA bundle when installed; curl fallback on Windows."""
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    ctx = _urllib_ssl_context()
    try:
        with urllib.request.urlopen(req, context=ctx, timeout=timeout) as response, open(
            dest_path, "wb"
        ) as out_file:
            shutil.copyfileobj(response, out_file)
        return
    except (urllib.error.URLError, OSError) as urllib_error:
        if sys.platform == "win32":
            try:
                _download_with_curl(url, dest_path, timeout)
                return
            except (OSError, subprocess.CalledProcessError) as curl_error:
                raise RuntimeError(
                    f"Failed to download {url}\n"
                    f"  urllib: {urllib_error}\n"
                    f"  curl: {curl_error}\n"
                    "Try: pip install certifi"
                ) from curl_error
        raise RuntimeError(
            f"Failed to download {url}: {urllib_error}\nTry: pip install certifi"
        ) from urllib_error
