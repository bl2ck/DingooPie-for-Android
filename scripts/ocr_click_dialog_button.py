import argparse
import csv
import io
import json
import subprocess
import tempfile
from pathlib import Path

import cv2
import numpy as np


def recognize(tesseract, image, origin_x, origin_y, scale, target):
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        cv2.imwrite(str(temporary_path), image)
        result = subprocess.run(
            [
                tesseract,
                str(temporary_path),
                "stdout",
                "--psm",
                "11",
                "tsv",
                "-c",
                "tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        rows = csv.DictReader(
            io.StringIO(result.stdout.decode("utf-8", "ignore")), delimiter="\t"
        )
        candidates = []
        for row in rows:
            if row["text"].strip().casefold() != target.casefold():
                continue
            confidence = float(row["conf"])
            if confidence < 50:
                continue
            center_x = origin_x + (
                int(row["left"]) + int(row["width"]) / 2
            ) / scale
            center_y = origin_y + (
                int(row["top"]) + int(row["height"]) / 2
            ) / scale
            candidates.append((confidence, center_x, center_y))
        return candidates
    finally:
        temporary_path.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--tesseract", required=True)
    args = parser.parse_args()

    image = cv2.imread(args.image)
    if image is None:
        raise SystemExit(f"Unable to read screenshot: {args.image}")

    height, width = image.shape[:2]
    gamma_table = np.array(
        [((value / 255.0) ** 0.5) * 255 for value in range(256)],
        dtype=np.uint8,
    )
    enhanced = cv2.LUT(image, gamma_table)
    regions = [
        (int(width * 0.36), int(height * 0.56),
         int(width * 0.64), int(height * 0.72), 6),
        (int(width * 0.20), int(height * 0.50),
         int(width * 0.80), int(height * 0.75), 5),
        (int(width * 0.30), int(height * 0.54),
         int(width * 0.70), int(height * 0.73), 6),
        (0, 0, width, height, 1),
    ]

    candidates = []
    for left, top, right, bottom, scale in regions:
        cropped = enhanced[top:bottom, left:right]
        resized = cv2.resize(
            cropped, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC
        )
        grayscale = cv2.cvtColor(resized, cv2.COLOR_BGR2GRAY)
        variants = [resized, grayscale]
        for threshold in (80, 100, 120, 140):
            _, binary = cv2.threshold(
                grayscale, threshold, 255, cv2.THRESH_BINARY
            )
            variants.append(binary)
        for variant in variants:
            candidates.extend(recognize(
                args.tesseract, variant, left, top, scale, args.target
            ))

    minimum_y = height * 0.50
    maximum_y = height * 0.75
    candidates = [
        candidate for candidate in candidates
        if minimum_y <= candidate[2] <= maximum_y
    ]
    if not candidates:
        raise SystemExit(f'OCR did not find dialog button "{args.target}"')

    expected_y = height * 0.63
    confidence, center_x, center_y = min(
        candidates,
        key=lambda candidate: (abs(candidate[2] - expected_y), -candidate[0]),
    )
    print(json.dumps({
        "text": args.target,
        "confidence": confidence,
        "x": round(center_x),
        "y": round(center_y),
    }))


if __name__ == "__main__":
    main()
