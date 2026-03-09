#!/usr/bin/env python3
import argparse
import asyncio
import random
import signal
import statistics
import time
from dataclasses import dataclass, field
from typing import List


@dataclass
class Stats:
    connect_ok: int = 0
    connect_fail: int = 0
    send_ok: int = 0
    send_fail: int = 0
    recv_bytes: int = 0
    active_clients: int = 0
    idle_clients: int = 0
    connect_latency_ms: List[float] = field(default_factory=list)


def positive_int(value: str) -> int:
    ivalue = int(value)
    if ivalue <= 0:
        raise argparse.ArgumentTypeError(f"must be > 0, got {value}")
    return ivalue


def ratio_0_1(value: str) -> float:
    fvalue = float(value)
    if fvalue < 0 or fvalue > 1:
        raise argparse.ArgumentTypeError(f"must be in [0,1], got {value}")
    return fvalue


async def idle_reader(reader: asyncio.StreamReader, stats: Stats, stop_evt: asyncio.Event):
    while not stop_evt.is_set():
        try:
            data = await asyncio.wait_for(reader.read(4096), timeout=1.0)
            if not data:
                return
            stats.recv_bytes += len(data)
        except asyncio.TimeoutError:
            continue
        except Exception:
            return


async def active_sender(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    stats: Stats,
    stop_evt: asyncio.Event,
    send_interval: float,
    payload: bytes,
):
    while not stop_evt.is_set():
        try:
            writer.write(payload)
            await writer.drain()
            stats.send_ok += 1

            try:
                data = await asyncio.wait_for(reader.read(4096), timeout=0.2)
                if data:
                    stats.recv_bytes += len(data)
            except asyncio.TimeoutError:
                pass

            await asyncio.sleep(send_interval)
        except Exception:
            stats.send_fail += 1
            return


async def one_client(
    idx: int,
    host: str,
    port: int,
    timeout: float,
    is_active: bool,
    send_interval: float,
    payload: bytes,
    stats: Stats,
    stop_evt: asyncio.Event,
    sem: asyncio.Semaphore,
):
    async with sem:
        writer = None
        try:
            t0 = time.perf_counter()
            reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), timeout=timeout)
            stats.connect_ok += 1
            stats.connect_latency_ms.append((time.perf_counter() - t0) * 1000)

            if is_active:
                await active_sender(reader, writer, stats, stop_evt, send_interval, payload)
            else:
                await idle_reader(reader, stats, stop_evt)
        except Exception:
            stats.connect_fail += 1
        finally:
            if writer is not None:
                try:
                    writer.close()
                    await writer.wait_closed()
                except Exception:
                    pass


async def main_async(args):
    stats = Stats()
    stop_evt = asyncio.Event()

    active_n = int(args.conn * args.active_ratio)
    idle_n = args.conn - active_n
    stats.active_clients = active_n
    stats.idle_clients = idle_n

    payload = ("x" * args.payload_size).encode("ascii")
    sem = asyncio.Semaphore(args.open_concurrency)

    print(f"[+] target: {args.host}:{args.port}")
    print(f"[+] total connections: {args.conn}")
    print(f"[+] active ratio: {args.active_ratio:.2f} -> active={active_n}, idle={idle_n}")
    print(f"[+] connect concurrency: {args.open_concurrency}")

    active_set = set(random.sample(range(args.conn), active_n)) if active_n > 0 else set()

    tasks = [
        asyncio.create_task(
            one_client(
                idx=i,
                host=args.host,
                port=args.port,
                timeout=args.connect_timeout,
                is_active=(i in active_set),
                send_interval=args.send_interval,
                payload=payload,
                stats=stats,
                stop_evt=stop_evt,
                sem=sem,
            )
        )
        for i in range(args.conn)
    ]

    loop = asyncio.get_running_loop()

    def _stop():
        stop_evt.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _stop)
        except NotImplementedError:
            pass

    start = time.perf_counter()

    # 等待连接尽可能建立完成（上限由 connect_timeout + 调度开销决定）
    await asyncio.sleep(args.connect_wait)

    established = stats.connect_ok
    failed = stats.connect_fail
    print(f"[+] established after {args.connect_wait:.1f}s: ok={established}, fail={failed}")

    await asyncio.sleep(args.duration)
    stop_evt.set()

    await asyncio.gather(*tasks, return_exceptions=True)
    elapsed = time.perf_counter() - start

    latency = stats.connect_latency_ms
    p50 = statistics.median(latency) if latency else 0
    p95 = statistics.quantiles(latency, n=100)[94] if len(latency) >= 100 else (max(latency) if latency else 0)

    print("\n===== C10K Report =====")
    print(f"runtime_sec        : {elapsed:.2f}")
    print(f"connect_ok         : {stats.connect_ok}")
    print(f"connect_fail       : {stats.connect_fail}")
    print(f"active_clients     : {stats.active_clients}")
    print(f"idle_clients       : {stats.idle_clients}")
    print(f"send_ok            : {stats.send_ok}")
    print(f"send_fail          : {stats.send_fail}")
    print(f"recv_bytes         : {stats.recv_bytes}")
    if elapsed > 0:
        print(f"send_qps           : {stats.send_ok / elapsed:.2f}")
        print(f"recv_MBps          : {stats.recv_bytes / elapsed / (1024*1024):.3f}")
    print(f"connect_latency_p50_ms: {p50:.2f}")
    print(f"connect_latency_p95_ms: {p95:.2f}")


def parse_args():
    p = argparse.ArgumentParser(description="Async C10K connection/concurrency test client")
    p.add_argument("--host", default="127.0.0.1", help="server host")
    p.add_argument("--port", type=positive_int, default=8888, help="server port")
    p.add_argument("--conn", type=positive_int, default=1000, help="total concurrent connections")
    p.add_argument("--active-ratio", type=ratio_0_1, default=0.1, help="active clients ratio [0,1]")
    p.add_argument("--duration", type=positive_int, default=60, help="test duration in seconds (after connect_wait)")
    p.add_argument("--connect-timeout", type=float, default=3.0, help="single connect timeout in seconds")
    p.add_argument("--connect-wait", type=float, default=8.0, help="wait seconds before starting duration timer report")
    p.add_argument("--open-concurrency", type=positive_int, default=200, help="max concurrent connect attempts")
    p.add_argument("--send-interval", type=float, default=1.0, help="seconds between sends for active clients")
    p.add_argument("--payload-size", type=positive_int, default=32, help="bytes per send for active clients")
    return p.parse_args()


if __name__ == "__main__":
    args = parse_args()
    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        pass
