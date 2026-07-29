#!/usr/bin/env python3
import json
import pathlib
import sys


def main() -> int:
    request = pathlib.Path(sys.argv[sys.argv.index("--request") + 1])
    result = pathlib.Path(sys.argv[sys.argv.index("--result") + 1])
    request_json = json.loads(request.read_text(encoding="utf-8"))
    result.write_text(
        json.dumps(
            {
                "result_type": "report_created",
                "message": "tool ok",
                "request": request_json,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
