import argparse
import time


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=30.0)
    args = parser.parse_args()
    time.sleep(max(0.0, args.seconds))
