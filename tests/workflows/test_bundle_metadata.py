"""Keep fork bundle metadata aligned with retained Slack client features."""

import plistlib
from pathlib import Path


root = Path(__file__).resolve().parents[2]
bundle = plistlib.loads((root / "src/app/resources/Info.plist.in").read_bytes())
assert "NSMicrophoneUsageDescription" not in bundle, "Legacy dictation permission remains"
schemes = {
    scheme
    for entry in bundle.get("CFBundleURLTypes", [])
    for scheme in entry.get("CFBundleURLSchemes", [])
}
assert "msga" in schemes, "Slack OAuth callback scheme is missing"
print("Fork bundle metadata checks passed.")
