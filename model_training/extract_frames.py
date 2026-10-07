# Extracts images into a separate session without overwriting previous captures.
# Records the source video's path and hash so the annotator can identify it safely.
# The annotator can call extract_frames() directly when the user opens a video.
from pathlib import Path
import argparse
import hashlib
import json
import uuid


def file_hash(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def extract_frames(video_path, every=10, frames_root=None):
    import cv2

    if every < 1:
        raise ValueError("The interval must be at least 1 frame.")
    video_path = Path(video_path).resolve(strict=True)
    if frames_root is None:
        frames_root = Path(__file__).resolve().parent / "results" / "frames"
    frames_root = Path(frames_root).resolve()
    session_id = uuid.uuid4().hex
    folder = frames_root / (video_path.stem + "_" + session_id + "_frames")
    video = cv2.VideoCapture(str(video_path))
    if not video.isOpened():
        video.release()
        raise ValueError(f"Could not open {video_path}")
    before = file_hash(video_path)
    folder.mkdir(parents=True, exist_ok=False)
    metadata = {
        "session_id": session_id,
        "video": str(video_path),
        "video_sha256": before,
        "complete": False,
    }
    metadata_path = folder / "session.json"
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    index = 0
    saved = 0
    try:
        while True:
            ok, frame = video.read()
            if not ok:
                break
            if index % every == 0:
                destination = folder / f"frame_{index:09d}.jpg"
                if not cv2.imwrite(str(destination), frame):
                    raise OSError(f"Could not save {destination}")
                saved += 1
            index += 1
    finally:
        video.release()
    if saved == 0:
        raise ValueError("The video produced no images.")
    if file_hash(video_path) != before:
        raise ValueError("The video changed during extraction. It was preserved.")
    metadata["complete"] = True
    metadata["frames"] = saved
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return folder


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", help="Video file")
    parser.add_argument("--every", type=int, default=10,
                        help="Save 1 frame every N frames (default: 10)")
    args = parser.parse_args()
    print(extract_frames(args.video, args.every))


if __name__ == "__main__":
    main()
