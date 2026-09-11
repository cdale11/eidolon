"""Export approved internet reading material from memory.db for offline training.

The runtime never trains from live web pages directly. It archives approved/fetched pages
as bounded resources; offline tooling can then turn that durable corpus into datasets for
future cognitive model improvements.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
from pathlib import Path
from typing import Iterable


def iter_resources(db_path: str | Path, min_chars: int = 1) -> Iterable[dict]:
    conn = sqlite3.connect(str(db_path))
    try:
        rows = conn.execute(
            """
            SELECT id, t, url, title, content, source
            FROM internet_resources
            WHERE length(content) >= ?
            ORDER BY t ASC, id ASC
            """,
            (max(1, int(min_chars)),),
        )
        for rid, t, url, title, content, source in rows:
            yield {
                "id": rid,
                "t": t,
                "url": url or "",
                "title": title or "",
                "content": content or "",
                "source": source or "",
            }
    finally:
        conn.close()


def export_jsonl(db_path: str | Path, out_path: str | Path, min_chars: int = 1) -> int:
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    n = 0
    with out.open("w", encoding="utf-8") as f:
        for item in iter_resources(db_path, min_chars=min_chars):
            f.write(json.dumps(item, ensure_ascii=False) + "\n")
            n += 1
    return n


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--db", required=True, help="Path to memory.db")
    p.add_argument("--out", required=True, help="Output JSONL path")
    p.add_argument("--min-chars", type=int, default=1)
    args = p.parse_args(argv)
    n = export_jsonl(args.db, args.out, min_chars=args.min_chars)
    print(f"exported {n} internet resources to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
