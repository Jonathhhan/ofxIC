"""Process-isolated Meta SAM point-prompt runner for the ofxIC bridge."""

from pathlib import Path
import argparse
import sys


def model_type(checkpoint):
    name = checkpoint.name.lower()
    if "vit_b" in name:
        return "vit_b"
    if "vit_l" in name:
        return "vit_l"
    if "vit_h" in name:
        return "vit_h"
    raise ValueError("checkpoint name must contain vit_b, vit_l, or vit_h")


def write_pgm(path, mask):
    pixels = (mask.astype("uint8") * 255).tobytes()
    height, width = mask.shape
    path.write_bytes(f"P5\n{width} {height}\n255\n".encode("ascii") + pixels)


def main():
    parser = argparse.ArgumentParser(description="ofxIC Meta SAM point runner")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backend", choices=("cpu", "cuda"), default="cuda")
    parser.add_argument("--point-x", type=float, action="append", required=True)
    parser.add_argument("--point-y", type=float, action="append", required=True)
    parser.add_argument(
        "--point-label", choices=("positive", "negative"), action="append", required=True)
    args = parser.parse_args()

    point_count = len(args.point_x)
    if len(args.point_y) != point_count or len(args.point_label) != point_count:
        parser.error("point x, y, and label counts differ")
    if not args.model.is_file() or not args.image.is_file():
        parser.error("model and image must be existing files")
    if any(not 0.0 <= value <= 1.0 for value in args.point_x + args.point_y):
        parser.error("point coordinates must be normalized")

    try:
        import numpy as np
        import torch
        from PIL import Image
        from segment_anything import SamPredictor, sam_model_registry

        if args.backend == "cuda" and not torch.cuda.is_available():
            raise RuntimeError(
                "CUDA was requested but the installed PyTorch runtime cannot access the GPU")
        device = args.backend
        image = np.asarray(Image.open(args.image).convert("RGB"))
        height, width = image.shape[:2]
        points = np.asarray(
            [[x * (width - 1), y * (height - 1)]
             for x, y in zip(args.point_x, args.point_y)], dtype=np.float32)
        labels = np.asarray(
            [1 if label == "positive" else 0 for label in args.point_label],
            dtype=np.int32)

        sam = sam_model_registry[model_type(args.model)](checkpoint=str(args.model))
        sam.to(device=device)
        predictor = SamPredictor(sam)
        predictor.set_image(image)
        masks, scores, _ = predictor.predict(
            point_coords=points, point_labels=labels, multimask_output=True)
        write_pgm(args.output, masks[int(np.argmax(scores))])
    except Exception as error:
        print(f"SAM runner failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
