#!/usr/bin/env python3

import argparse
import csv
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path


def read_wave(path):
    file_data = path.read_bytes()
    if len(file_data) < 44 or file_data[:4] != b"RIFF" or file_data[8:12] != b"WAVE":
        raise ValueError("not a PCM WAV file")
    format_marker = file_data.find(b"fmt ", 12)
    if format_marker < 0 or format_marker + 24 > len(file_data):
        raise ValueError("WAV format chunk was not found")
    audio_format, channels, sample_rate = struct.unpack_from(
        "<HHI", file_data, format_marker + 8)
    bits_per_sample = struct.unpack_from("<H", file_data, format_marker + 22)[0]
    if audio_format != 1 or bits_per_sample not in (8, 16):
        raise ValueError(
            f"unsupported WAV format={audio_format} bits={bits_per_sample}")
    sample_width = bits_per_sample // 8
    data_marker = file_data.find(b"data", 12)
    if data_marker < 0 or data_marker + 8 > len(file_data):
        raise ValueError("WAV data chunk was not found")
    raw = file_data[data_marker + 8:]
    if sample_width not in (1, 2):
        raise ValueError(f"unsupported sample width: {sample_width}")
    frame_size = channels * sample_width
    raw = raw[:len(raw) - len(raw) % frame_size]
    frame_count = len(raw) // frame_size
    samples = []
    for offset in range(0, len(raw) - frame_size + 1, frame_size):
        total = 0.0
        for channel in range(channels):
            position = offset + channel * sample_width
            if sample_width == 1:
                value = raw[position] - 128
                total += value / 128.0
            else:
                value = struct.unpack_from("<h", raw, position)[0]
                total += value / 32768.0
        samples.append(total / channels)
    return {
        "channels": channels,
        "sample_width": sample_width,
        "sample_rate": sample_rate,
        "frame_count": frame_count,
        "duration_seconds": frame_count / sample_rate if sample_rate else 0.0,
        "raw": raw,
        "samples": samples,
    }


def read_events(path):
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def write_png(path, width, height, pixels):
    def chunk(name, data):
        return (struct.pack(">I", len(data)) + name + data +
                struct.pack(">I", zlib.crc32(name + data) & 0xFFFFFFFF))

    rows = bytearray()
    stride = width * 3
    for y in range(height):
        rows.append(0)
        rows.extend(pixels[y * stride:(y + 1) * stride])
    payload = bytearray(b"\x89PNG\r\n\x1a\n")
    payload.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
    payload.extend(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
    payload.extend(chunk(b"IEND", b""))
    path.write_bytes(payload)


def set_pixel(pixels, width, height, x, y, color):
    if x < 0 or y < 0 or x >= width or y >= height:
        return
    offset = (y * width + x) * 3
    pixels[offset:offset + 3] = bytes(color)


def draw_line(pixels, width, height, x0, y0, x1, y1, color):
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    step_x = 1 if x0 < x1 else -1
    step_y = 1 if y0 < y1 else -1
    error = dx + dy
    while True:
        set_pixel(pixels, width, height, x0, y0, color)
        if x0 == x1 and y0 == y1:
            break
        twice = 2 * error
        if twice >= dy:
            error += dy
            x0 += step_x
        if twice <= dx:
            error += dx
            y0 += step_y


def render_waveform(samples, path):
    width, height = 1200, 360
    pixels = bytearray([18, 21, 28] * width * height)
    center = height // 2
    draw_line(pixels, width, height, 0, center, width - 1, center, (70, 78, 92))
    if samples:
        for x in range(width):
            begin = x * len(samples) // width
            end = max(begin + 1, (x + 1) * len(samples) // width)
            segment = samples[begin:end]
            low = max(-1.0, min(segment))
            high = min(1.0, max(segment))
            y0 = center - int(high * (center - 12))
            y1 = center - int(low * (center - 12))
            draw_line(pixels, width, height, x, y0, x, y1, (63, 205, 255))
    write_png(path, width, height, pixels)


def fft(values):
    size = len(values)
    output = [complex(value, 0.0) for value in values]
    index = 0
    for current in range(1, size):
        bit = size >> 1
        while index & bit:
            index ^= bit
            bit >>= 1
        index ^= bit
        if current < index:
            output[current], output[index] = output[index], output[current]
    length = 2
    while length <= size:
        angle = -2.0 * math.pi / length
        root = complex(math.cos(angle), math.sin(angle))
        half = length // 2
        for start in range(0, size, length):
            factor = 1.0 + 0.0j
            for offset in range(half):
                even = output[start + offset]
                odd = output[start + offset + half] * factor
                output[start + offset] = even + odd
                output[start + offset + half] = even - odd
                factor *= root
        length *= 2
    return output


def heat_color(level):
    level = max(0.0, min(1.0, level))
    if level < 0.33:
        scale = level / 0.33
        return (0, int(80 * scale), int(70 + 185 * scale))
    if level < 0.66:
        scale = (level - 0.33) / 0.33
        return (int(50 * scale), int(80 + 175 * scale), int(255 - 95 * scale))
    scale = (level - 0.66) / 0.34
    return (int(50 + 205 * scale), 255, int(160 - 130 * scale))


def render_spectrogram(samples, sample_rate, path):
    width, height = 1000, 400
    pixels = bytearray([8, 10, 18] * width * height)
    if not samples or sample_rate <= 0:
        write_png(path, width, height, pixels)
        return
    fft_size = 512
    max_frequency = min(8000.0, sample_rate / 2.0)
    max_bin = max(1, min(fft_size // 2, int(max_frequency * fft_size / sample_rate)))
    columns = min(width, max(1, len(samples) // max(1, fft_size // 2)))
    spectra = []
    maximum = -120.0
    for column in range(columns):
        center = column * max(1, len(samples) - fft_size) // max(1, columns - 1)
        window = samples[center:center + fft_size]
        if len(window) < fft_size:
            window += [0.0] * (fft_size - len(window))
        window = [value * (0.5 - 0.5 * math.cos(2.0 * math.pi * index / (fft_size - 1)))
                  for index, value in enumerate(window)]
        transformed = fft(window)
        magnitudes = [20.0 * math.log10(abs(transformed[index]) + 1e-9)
                      for index in range(max_bin)]
        maximum = max(maximum, max(magnitudes))
        spectra.append(magnitudes)
    floor = maximum - 70.0
    for x in range(width):
        source_x = min(columns - 1, x * columns // width)
        values = spectra[source_x]
        for y in range(height):
            source_bin = min(max_bin - 1, (height - 1 - y) * max_bin // height)
            level = (values[source_bin] - floor) / max(1.0, maximum - floor)
            set_pixel(pixels, width, height, x, y, heat_color(level))
    write_png(path, width, height, pixels)


def calculate_metrics(audio, events):
    samples = audio["samples"]
    sample_rate = audio["sample_rate"]
    duration = audio["duration_seconds"]
    peak = max((abs(value) for value in samples), default=0.0)
    rms = math.sqrt(sum(value * value for value in samples) / len(samples)) if samples else 0.0
    discontinuities = sum(1 for left, right in zip(samples, samples[1:])
                          if abs(right - left) >= 0.65)

    silence_threshold = max(0.0025, rms * 0.025)
    minimum_silence = max(1, int(sample_rate * 0.03))
    silence_runs = []
    run_start = None
    for index, value in enumerate(samples):
        if abs(value) <= silence_threshold:
            if run_start is None:
                run_start = index
        elif run_start is not None:
            if index - run_start >= minimum_silence:
                silence_runs.append((run_start, index))
            run_start = None
    if run_start is not None and len(samples) - run_start >= minimum_silence:
        silence_runs.append((run_start, len(samples)))

    block_frames = max(1, int(sample_rate * 0.02))
    frame_bytes = audio["channels"] * audio["sample_width"]
    block_bytes = block_frames * frame_bytes
    repeated_blocks = 0
    longest_repeat = 0
    previous_hash = None
    current_repeat = 0
    for offset in range(0, len(audio["raw"]) - block_bytes + 1, block_bytes):
        block = audio["raw"][offset:offset + block_bytes]
        digest = hashlib.sha1(block).digest()
        if digest == previous_hash and any(block):
            current_repeat += 1
            repeated_blocks += 1
            longest_repeat = max(longest_repeat, current_repeat)
        else:
            current_repeat = 0
        previous_hash = digest

    event_counts = {}
    waits = []
    write_times = []
    write_bytes = []
    input_events = []
    last_elapsed_ms = 0
    first_elapsed_ms = None
    zero_queue_writes = 0
    write_events = 0
    device_buffer_bytes = 0
    for event in events:
        name = event.get("event", "")
        event_counts[name] = event_counts.get(name, 0) + 1
        elapsed = int(event.get("elapsed_ms") or 0)
        last_elapsed_ms = max(last_elapsed_ms, elapsed)
        if name == "wait":
            waits.append(int(event.get("wait_ms") or 0))
        if name == "open":
            device_buffer_bytes = int(event.get("bytes") or 0)
        if name == "input":
            input_events.append((elapsed, int(event.get("queued_bytes") or 0)))
        if name in ("queue", "pending"):
            write_times.append(elapsed)
            write_bytes.append(int(event.get("bytes") or 0))
            write_events += 1
            if int(event.get("queued_bytes") or 0) == 0:
                zero_queue_writes += 1

    if write_times:
        first_elapsed_ms = write_times[0]
        wall_duration = (write_times[-1] - first_elapsed_ms) / 1000.0
    else:
        wall_duration = last_elapsed_ms / 1000.0
    duration_ratio = duration / wall_duration if wall_duration > 0 else 0.0
    gaps = [right - left for left, right in zip(write_times, write_times[1:])]
    sorted_gaps = sorted(gaps)
    gap_p50 = sorted_gaps[len(sorted_gaps) // 2] if sorted_gaps else 0
    gap_p90 = sorted_gaps[int(len(sorted_gaps) * 0.90)] if sorted_gaps else 0
    gap_p99 = sorted_gaps[int(len(sorted_gaps) * 0.99)] if sorted_gaps else 0
    bytes_per_second = sample_rate * audio["channels"] * audio["sample_width"]
    typical_bytes = sorted(write_bytes)[len(write_bytes) // 2] if write_bytes else 0
    typical_buffer_ms = typical_bytes * 1000.0 / bytes_per_second if bytes_per_second else 0.0
    device_buffer_ms = (device_buffer_bytes * 1000.0 / bytes_per_second
                        if bytes_per_second else 0.0)
    input_response_ms = []
    for input_elapsed, queued_bytes in input_events:
        next_write = next((elapsed for elapsed in write_times if elapsed >= input_elapsed), None)
        if next_write is not None and bytes_per_second:
            queued_ms = queued_bytes * 1000.0 / bytes_per_second
            input_response_ms.append(next_write - input_elapsed + queued_ms + device_buffer_ms)
    sorted_input_response_ms = sorted(input_response_ms)
    input_response_p50 = (sorted_input_response_ms[len(sorted_input_response_ms) // 2]
                          if sorted_input_response_ms else 0.0)
    input_response_p90 = (sorted_input_response_ms[
        min(len(sorted_input_response_ms) - 1, int(len(sorted_input_response_ms) * 0.90))]
        if sorted_input_response_ms else 0.0)
    warnings = []
    failures = []
    if event_counts.get("drop", 0):
        failures.append("audio buffers were dropped")
    if event_counts.get("queue_error", 0):
        failures.append("SDL queue errors were recorded")
    if not samples or rms < 0.0001:
        failures.append("captured audio is empty or silent")
    max_wait = max(waits, default=0)
    if max_wait > 250:
        warnings.append(f"maximum queue wait is {max_wait} ms")
    if wall_duration > 2.0 and not 0.90 <= duration_ratio <= 1.10:
        warnings.append(f"audio/wall duration ratio is {duration_ratio:.3f}")
    if write_events >= 20 and zero_queue_writes / write_events > 0.15:
        warnings.append(
            f"audio queue was empty before {zero_queue_writes / write_events:.1%} of writes")
    if (typical_buffer_ms and gap_p99 > typical_buffer_ms * 2.0 and
            write_events and zero_queue_writes / write_events > 0.15):
        warnings.append(
            f"99th percentile write gap is {gap_p99} ms for a {typical_buffer_ms:.1f} ms buffer")
    longest_repeat_ms = longest_repeat * 20
    if longest_repeat_ms >= 200:
        warnings.append(f"identical PCM blocks repeat for {longest_repeat_ms} ms")
    status = "fail" if failures else ("warning" if warnings else "pass")
    return {
        "status": status,
        "sample_rate": sample_rate,
        "channels": audio["channels"],
        "sample_width_bytes": audio["sample_width"],
        "frames": audio["frame_count"],
        "duration_seconds": round(duration, 4),
        "event_duration_seconds": round(wall_duration, 4),
        "audio_to_event_duration_ratio": round(duration_ratio, 4),
        "peak": round(peak, 6),
        "rms": round(rms, 6),
        "discontinuity_count": discontinuities,
        "silence_gap_count_30ms": len(silence_runs),
        "longest_silence_ms": round(max((end - start for start, end in silence_runs), default=0)
                                    * 1000.0 / sample_rate, 2) if sample_rate else 0,
        "repeated_block_count": repeated_blocks,
        "longest_identical_repeat_ms": longest_repeat_ms,
        "queue_wait_count": len(waits),
        "queue_wait_total_ms": sum(waits),
        "queue_wait_max_ms": max_wait,
        "write_gap_p50_ms": gap_p50,
        "write_gap_p90_ms": gap_p90,
        "write_gap_p99_ms": gap_p99,
        "write_gap_max_ms": max(gaps, default=0),
        "typical_buffer_duration_ms": round(typical_buffer_ms, 3),
        "device_buffer_duration_ms": round(device_buffer_ms, 3),
        "input_response_count": len(input_response_ms),
        "estimated_input_response_p50_ms": round(input_response_p50, 3),
        "estimated_input_response_p90_ms": round(input_response_p90, 3),
        "estimated_input_response_max_ms": round(max(input_response_ms, default=0.0), 3),
        "zero_queue_write_ratio": round(zero_queue_writes / write_events, 4)
                                  if write_events else 0.0,
        "event_counts": event_counts,
        "warnings": warnings,
        "failures": failures,
    }


def write_html(path, metrics):
    rows = "".join(
        f"<tr><th>{key}</th><td>{value}</td></tr>"
        for key, value in metrics.items()
        if key not in ("warnings", "failures", "event_counts")
    )
    messages = "".join(f"<li>{message}</li>"
                       for message in metrics["failures"] + metrics["warnings"])
    path.write_text(f"""<!doctype html>
<meta charset="utf-8">
<title>DingooPie Audio Validation</title>
<style>
body{{font:16px sans-serif;background:#151821;color:#eef2f7;margin:28px}}
img{{max-width:100%;border:1px solid #586174;margin:8px 0 24px}}
table{{border-collapse:collapse}}th,td{{border:1px solid #586174;padding:7px 12px;text-align:left}}
.status{{font-size:24px;color:{'#ff6b6b' if metrics['status']=='fail' else '#ffd166' if metrics['status']=='warning' else '#5ce1a4'}}}
</style>
<h1>DingooPie Audio Validation</h1>
<div class="status">{metrics['status'].upper()}</div>
<h2>Waveform</h2><img src="audio-waveform.png">
<h2>Spectrogram</h2><img src="audio-spectrogram.png">
<h2>Metrics</h2><table>{rows}</table>
<h2>Findings</h2><ul>{messages or '<li>No threshold violations.</li>'}</ul>
""", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--wav", required=True, type=Path)
    parser.add_argument("--events", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    audio = read_wave(args.wav)
    events = read_events(args.events)
    metrics = calculate_metrics(audio, events)
    render_waveform(audio["samples"], args.output / "audio-waveform.png")
    render_spectrogram(audio["samples"], audio["sample_rate"],
                       args.output / "audio-spectrogram.png")
    (args.output / "audio-validation-report.json").write_text(
        json.dumps(metrics, ensure_ascii=False, indent=2), encoding="utf-8")
    write_html(args.output / "audio-validation-report.html", metrics)
    print(json.dumps(metrics, ensure_ascii=False, indent=2))
    return 1 if metrics["status"] == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
