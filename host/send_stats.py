#!/usr/bin/env python3
"""Legacy Python collector. Prefer the Go host: `go build -o rainmonitor .` in this directory."""

from __future__ import annotations

import argparse
import os
import platform
import re
import subprocess
import sys
import time
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.stderr.write("Install host dependencies first: pip3 install -r host/requirements.txt\n")
    raise

try:
    import psutil
except ImportError:
    psutil = None


def cpu_percent(interval: float) -> float:
    if psutil is not None:
        return float(psutil.cpu_percent(interval=interval))
    time.sleep(interval)
    if hasattr(os, "getloadavg"):
        load = os.getloadavg()[0]
        ncpu = os.cpu_count() or 1
        return max(0.0, min(100.0, 100.0 * load / ncpu))
    return 0.0


def gpu_from_ioreg() -> float | None:
    try:
        out = subprocess.check_output(
            ["ioreg", "-r", "-d", "1", "-w", "0", "-c", "IOAccelerator"],
            text=True,
            timeout=2,
        )
    except (OSError, subprocess.SubprocessError):
        return None

    for key in (
        "Device Utilization %",
        "Renderer Utilization %",
        "Tiler Utilization %",
        "gpu-core-utilization",
    ):
        match = re.search(rf'"{re.escape(key)}"\s*=\s*(\d+(?:\.\d+)?)', out)
        if match:
            return float(match.group(1))
    return None


def gpu_from_powermetrics() -> float | None:
    try:
        out = subprocess.check_output(
            ["powermetrics", "--samplers", "gpu_power", "-n", "1", "-i", "200"],
            text=True,
            timeout=4,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    match = re.search(r"GPU (?:HW )?active residency:\s*(\d+(?:\.\d+)?)%", out)
    if match:
        return float(match.group(1))
    return None


def gpu_from_nvidia() -> float | None:
    try:
        out = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"],
            text=True,
            timeout=2,
        )
        values = [float(line.strip()) for line in out.splitlines() if line.strip()]
        return max(values) if values else None
    except (OSError, subprocess.SubprocessError, ValueError):
        return None


def gpu_from_windows() -> float | None:
    if os.name != "nt":
        return None
    try:
        import wmi  # type: ignore

        c = wmi.WMI(namespace="root\\cimv2")
        counters = c.Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine()
        util = [float(x.UtilizationPercentage) for x in counters if x.UtilizationPercentage]
        return max(util) if util else None
    except Exception:
        return None


def read_gpu() -> float | None:
    system = platform.system()
    if system == "Darwin":
        return gpu_from_ioreg() or gpu_from_powermetrics() or gpu_from_nvidia()
    if system == "Windows":
        return gpu_from_nvidia() or gpu_from_windows()
    return gpu_from_nvidia()


def clamp_pct(value: float) -> float:
    return max(0.0, min(100.0, value))


class SmoothedGpu:
    def __init__(self) -> None:
        self.value: float | None = None

    def update(self, raw: float | None) -> float | None:
        if raw is None:
            return self.value
        sample = clamp_pct(raw)
        if self.value is None:
            self.value = sample
        else:
            self.value = self.value * 0.45 + sample * 0.55
        return self.value


def find_port(preferred: str | None) -> str:
    if preferred:
        return preferred
    ports = list(list_ports.comports())
    scored = []
    for port in ports:
        blob = f"{port.device} {port.description} {port.manufacturer} {port.product}".lower()
        score = 0
        for token in ("rainmonitor", "mosaico", "espressif", "usbmodem", "usbserial", "ttyacm"):
            if token in blob:
                score += 1
        if score:
            scored.append((score, port.device))
    if scored:
        scored.sort(reverse=True)
        return scored[0][1]
    if ports:
        return ports[0].device
    raise SystemExit("No serial port found. Plug in ESP-Mosaico or pass --port.")


def sanitize_model(name: str, limit: int = 36) -> str:
    cleaned = "".join(ch if 32 <= ord(ch) < 127 and ch not in '",=' else " " for ch in name)
    cleaned = " ".join(cleaned.split())
    return cleaned[:limit]


def read_cmd(*args: str, timeout: float = 4.0) -> str | None:
    try:
        return subprocess.check_output(args, text=True, timeout=timeout, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return None


def cpu_model() -> str:
    system = platform.system()
    if system == "Darwin":
        out = read_cmd("sysctl", "-n", "machdep.cpu.brand_string")
        if out and out.strip():
            return sanitize_model(out.strip())
    if system == "Windows":
        try:
            import wmi  # type: ignore

            name = wmi.WMI().Win32_Processor()[0].Name
            if name:
                return sanitize_model(name)
        except Exception:
            pass
        if platform.processor():
            return sanitize_model(platform.processor())
    try:
        with open("/proc/cpuinfo", encoding="utf-8", errors="ignore") as fh:
            for line in fh:
                if line.lower().startswith("model name"):
                    return sanitize_model(line.split(":", 1)[1].strip())
    except OSError:
        pass
    return "CPU"


def gpu_model() -> str:
    nvidia = read_cmd("nvidia-smi", "--query-gpu=name", "--format=csv,noheader")
    if nvidia:
        names = [line.strip() for line in nvidia.splitlines() if line.strip()]
        if names:
            return sanitize_model(names[0])

    system = platform.system()
    if system == "Darwin":
        out = read_cmd("system_profiler", "SPDisplaysDataType", timeout=8)
        if out:
            match = re.search(r"Chipset Model:\s*(.+)", out)
            if match:
                return sanitize_model(match.group(1).strip())
    if system == "Windows":
        try:
            import wmi  # type: ignore

            for gpu in wmi.WMI().Win32_VideoController():
                if gpu.Name:
                    return sanitize_model(gpu.Name)
        except Exception:
            pass
    pci = read_cmd("lspci")
    if pci:
        match = re.search(r"(?:VGA|3D|Display)(?: compatible)? controller:\s*(.+)", pci)
        if match:
            return sanitize_model(match.group(1).strip())
    return "GPU"


def cpu_freq_mhz() -> float | None:
    if psutil is not None:
        freq = psutil.cpu_freq()
        if freq is not None and freq.current:
            current = float(freq.current)
            if 0.5 <= current <= 20.0:
                return current * 1000.0
            if current > 20.0:
                return current
    if platform.system() == "Windows":
        try:
            import wmi  # type: ignore

            mhz = wmi.WMI().Win32_Processor()[0].CurrentClockSpeed
            if mhz:
                return float(mhz)
        except Exception:
            pass
    try:
        with open("/proc/cpuinfo", encoding="utf-8", errors="ignore") as fh:
            found = None
            for line in fh:
                if "cpu mhz" in line.lower():
                    found = float(line.split(":", 1)[1].strip())
            if found:
                return found
    except (OSError, ValueError):
        pass
    return None


def memory_stats() -> tuple[float, float, float] | None:
    if psutil is None:
        return None
    vm = psutil.virtual_memory()
    used = (vm.total - vm.available) / (1024 ** 3)
    total = vm.total / (1024 ** 3)
    return float(vm.percent), used, total


class NetRates:
    def __init__(self) -> None:
        self.prev = None
        self.prev_t = 0.0

    def read(self) -> tuple[float, float]:
        if psutil is None:
            return 0.0, 0.0
        now = time.monotonic()
        io = psutil.net_io_counters()
        if self.prev is None:
            self.prev = io
            self.prev_t = now
            return 0.0, 0.0
        dt = max(now - self.prev_t, 0.001)
        up = max(0.0, (io.bytes_sent - self.prev.bytes_sent) / dt / 1024.0)
        down = max(0.0, (io.bytes_recv - self.prev.bytes_recv) / dt / 1024.0)
        self.prev = io
        self.prev_t = now
        return up, down


def encode_line(
    cpu: float,
    gpu: float | None,
    cpu_name: str,
    gpu_name: str,
    freq: float | None,
    mem: tuple[float, float, float] | None,
    net: tuple[float, float],
) -> bytes:
    parts = [f"cpu={cpu:.1f}"]
    if gpu is not None:
        parts.append(f"gpu={gpu:.1f}")
    if freq is not None:
        parts.append(f"cpu_freq={freq:.0f}")
    if mem is not None:
        parts.append(f"mem={mem[0]:.1f}")
        parts.append(f"mem_used={mem[1]:.2f}")
        parts.append(f"mem_total={mem[2]:.2f}")
    parts.append(f"net_up={net[0]:.1f}")
    parts.append(f"net_down={net[1]:.1f}")
    parts.append(f'time="{datetime.now().strftime("%H:%M:%S")}"')
    if cpu_name:
        parts.append(f'cpu_model="{cpu_name}"')
    if gpu_name:
        parts.append(f'gpu_model="{gpu_name}"')
    return (",".join(parts) + "\n").encode()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Serial port, such as /dev/cu.usbmodem3101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--interval", type=float, default=1.0, help="Sample period in seconds (Task Manager High = 1s)")
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()

    gpu_filter = SmoothedGpu()
    net_rates = NetRates()
    cpu_name = cpu_model()
    gpu_name = gpu_model()
    port_name = find_port(args.port)
    print(f"Opening {port_name}", flush=True)
    print(f"CPU {cpu_name}  GPU {gpu_name}", flush=True)
    with serial.Serial(port_name, args.baud, timeout=1) as ser:
        time.sleep(0.3)
        while True:
            cpu = clamp_pct(cpu_percent(args.interval))
            gpu = gpu_filter.update(read_gpu())
            freq = cpu_freq_mhz()
            mem = memory_stats()
            net = net_rates.read()
            ser.write(encode_line(cpu, gpu, cpu_name, gpu_name, freq, mem, net))
            ser.flush()
            gpu_text = "n/a" if gpu is None else f"{gpu:.1f}%"
            freq_text = "--" if freq is None else f"{freq / 1000.0:.2f}GHz"
            mem_text = "--" if mem is None else f"{mem[0]:.0f}%"
            print(
                f"cpu={cpu:5.1f}% {freq_text}  gpu={gpu_text}  mem={mem_text}  "
                f"dn={net[1]:.0f}KB/s up={net[0]:.0f}KB/s",
                end="\r",
                flush=True,
            )
            if args.once:
                print()
                return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print()
        raise SystemExit(0)
