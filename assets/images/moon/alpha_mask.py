#!/usr/bin/env python3
import argparse
import asyncio
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import aiohttp
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFont
from rich import box
from rich.console import Console
from rich.panel import Panel
from rich.progress import BarColumn, Progress, SpinnerColumn, TextColumn, TimeRemainingColumn
from rich.table import Table
from scipy import ndimage
from skimage.registration import phase_cross_correlation

console = Console()

# Astronomical constants
LUNAR_CYCLE_DAYS = 29.530588861  # Synodic month duration
LIBRATION_PERIOD_DAYS = 27.554551  # Period of lunar libration in longitude
MAX_LIBRATION_DEGREES = 7.9  # Maximum libration angle


def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description="🌙 Generate alpha masks from moon phase images for efficient storage and animation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s -i moon_phases_2025 -o output/masks
  %(prog)s --input ./images --output ./masks --full-moon 2098 --new-moon 310
  %(prog)s -i moon_data -o masks --scale 100x100 --no-video
  %(prog)s -i images -o masks --no-download --workers 4
        """,
    )

    parser.add_argument(
        "-i",
        "--input",
        required=True,
        type=str,
        help="Input directory containing moon phase TIFF images (required)",
    )

    parser.add_argument(
        "-o",
        "--output",
        required=True,
        type=str,
        help="Output directory for generated masks and base images (required)",
    )

    parser.add_argument(
        "--full-moon",
        type=int,
        default=None,
        help="Index of full moon reference image (default: auto-detect brightest)",
    )

    parser.add_argument(
        "--new-moon",
        type=int,
        default=None,
        help="Index of new moon reference image (default: auto-detect darkest)",
    )

    parser.add_argument(
        "--interval",
        type=int,
        default=24,
        help="Interval between phase images in hours (default: 24)",
    )

    parser.add_argument(
        "--days", type=float, default=LUNAR_CYCLE_DAYS, help="Number of days to process (default: 29.53)"
    )

    parser.add_argument(
        "--no-download",
        action="store_true",
        help="Skip automatic download of missing images",
    )

    parser.add_argument(
        "--no-video",
        action="store_true",
        help="Skip video creation (only generate masks)",
    )

    parser.add_argument(
        "--workers",
        type=int,
        default=None,
        help="Number of worker threads for processing (default: CPU count)",
    )

    parser.add_argument(
        "--scale",
        type=str,
        default=None,
        help="Scale moon to specific size (e.g., 100x100, 200x200). Scales moon region only for faster processing.",
    )

    return parser.parse_args()


def parse_scale_option(scale_str):
    """Parse scale option in format WIDTHxHEIGHT"""
    if not scale_str:
        return None

    try:
        width, height = scale_str.lower().split("x")
        return (int(width), int(height))
    except ValueError:
        console.print(f"[red]✗ Invalid scale format: {scale_str}. Expected format: WIDTHxHEIGHT (e.g., 100x100)")
        return None


async def download_moon_image(session, url, filepath, semaphore, progress, task_id):
    """Download a single moon image with retry logic"""
    if filepath.exists():
        progress.update(task_id, advance=1, description=f"[cyan]Skipped (exists)")
        return True

    async with semaphore:
        max_retries = 5
        for retry in range(max_retries):
            try:
                async with session.get(url, timeout=aiohttp.ClientTimeout(total=30)) as response:
                    if response.status == 200:
                        content = await response.read()
                        filepath.write_bytes(content)
                        progress.update(task_id, advance=1, description=f"[green]Downloaded")
                        return True
                    elif response.status == 404:
                        progress.update(task_id, advance=1, description=f"[yellow]Not found")
                        return False
            except Exception as e:
                if retry < max_retries - 1:
                    await asyncio.sleep(2**retry)
                else:
                    progress.update(task_id, advance=1, description=f"[red]Failed")
                    return False
    return False


async def download_missing_images(moon_dir, required_indices):
    """Download missing moon images asynchronously"""
    moon_dir = Path(moon_dir)
    moon_dir.mkdir(parents=True, exist_ok=True)

    base_url = "https://svs.gsfc.nasa.gov/vis/a000000/a005400/a005415/frames/1920x1080_16x9_30p/plain"

    # Check which files are missing
    to_download = []
    for idx in required_indices:
        filepath = moon_dir / f"moon.{idx:04d}.tif"
        if not filepath.exists():
            to_download.append((idx, filepath))

    if not to_download:
        console.print("[green]✓ All required images already downloaded")
        return True

    console.print(f"\n[yellow]Downloading {len(to_download)} missing images...")

    semaphore = asyncio.Semaphore(5)  # Limit concurrent downloads

    async with aiohttp.ClientSession() as session:
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
            TimeRemainingColumn(),
            console=console,
        ) as progress:
            task = progress.add_task("[cyan]Downloading images...", total=len(to_download))

            tasks = [
                download_moon_image(
                    session,
                    f"{base_url}/moon.{idx:04d}.tif",
                    filepath,
                    semaphore,
                    progress,
                    task,
                )
                for idx, filepath in to_download
            ]

            results = await asyncio.gather(*tasks)

    success_count = sum(results)
    console.print(f"[green]✓ Successfully downloaded {success_count}/{len(to_download)} images")
    return success_count == len(to_download)


def detect_moon_phase_extremes(moon_dir, sample_size=100):
    """Auto-detect full moon (brightest) and new moon (darkest) from available images"""
    console.print("\n[cyan]🔍 Auto-detecting moon phase extremes...")

    moon_dir = Path(moon_dir)
    tif_files = sorted(moon_dir.glob("moon.*.tif"))

    if not tif_files:
        raise ValueError(f"No moon TIFF files found in {moon_dir}")

    # Sample images evenly across the available range
    step = max(1, len(tif_files) // sample_size)
    sampled_files = tif_files[::step]

    brightnesses = []

    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
        console=console,
    ) as progress:
        task = progress.add_task("[cyan]Analyzing images...", total=len(sampled_files))

        for filepath in sampled_files:
            try:
                # Extract index from filename (moon.0001.tif -> 1)
                idx = int(filepath.stem.split(".")[1])

                img = Image.open(filepath).convert("RGB")
                img_array = np.array(img, dtype=np.float32)

                # Calculate average brightness of the moon region
                gray = np.mean(img_array[:, :, :3], axis=2)
                threshold = np.percentile(gray, 50)
                moon_mask = gray > threshold

                moon_brightness = np.mean(gray[moon_mask]) if np.any(moon_mask) else 0
                brightnesses.append((idx, moon_brightness))

                progress.update(task, advance=1)
            except Exception as e:
                console.print(f"[yellow]Warning: Could not process {filepath.name}: {e}")
                continue

    if not brightnesses:
        raise ValueError("Could not analyze any images")

    # Find extremes
    brightnesses.sort(key=lambda x: x[1])
    new_moon_idx = brightnesses[0][0]  # Darkest
    full_moon_idx = brightnesses[-1][0]  # Brightest

    result_table = Table(title="Auto-Detected Moon Phases", box=box.ROUNDED)
    result_table.add_column("Phase", style="cyan")
    result_table.add_column("Index", style="yellow")
    result_table.add_column("Brightness", style="green")
    result_table.add_row("New Moon (darkest)", str(new_moon_idx), f"{brightnesses[0][1]:.2f}")
    result_table.add_row("Full Moon (brightest)", str(full_moon_idx), f"{brightnesses[-1][1]:.2f}")
    console.print(result_table)

    return new_moon_idx, full_moon_idx


def calculate_lunar_libration_angle(hour_idx):
    """Calculate theoretical lunar libration angle based on astronomical data

    The Moon's libration in longitude follows a sinusoidal pattern with:
    - Period: ~27.55 days (LIBRATION_PERIOD_DAYS)
    - Amplitude: ±7.9° (MAX_LIBRATION_DEGREES)

    Args:
        hour_idx: Hour index from start of dataset (e.g., 0, 24, 48...)

    Returns:
        Libration angle in degrees (positive = rotated counterclockwise)
    """
    # Convert hour index to days
    days = hour_idx / 24.0

    # Calculate libration angle (sinusoidal with period LIBRATION_PERIOD_DAYS)
    # Phase offset may need calibration based on dataset start date
    angle = MAX_LIBRATION_DEGREES * np.sin(2 * np.pi * days / LIBRATION_PERIOD_DAYS)

    return angle


def rotate_image(img_array, angle_degrees):
    """Rotate image by given angle in degrees (counter-clockwise)"""
    from scipy.ndimage import rotate

    # Rotate each channel separately
    rotated = np.zeros_like(img_array)
    for c in range(img_array.shape[2]):
        rotated[:, :, c] = rotate(
            img_array[:, :, c],
            angle_degrees,
            reshape=False,
            mode="constant",
            cval=0,
            order=1,
        )

    return rotated


def find_moon_center_and_radius(img_array):
    """Find moon center and radius using edge detection"""
    # Convert to grayscale luminosity
    gray = np.mean(img_array[:, :, :3], axis=2)

    # Find bright region (moon)
    threshold = np.percentile(gray, 50)
    moon_mask = gray > threshold

    # Find center of mass
    cy, cx = ndimage.center_of_mass(moon_mask)

    # Estimate radius
    y_coords, x_coords = np.where(moon_mask)
    if len(x_coords) > 0:
        distances = np.sqrt((x_coords - cx) ** 2 + (y_coords - cy) ** 2)
        radius = np.percentile(distances, 95)
    else:
        radius = min(img_array.shape[:2]) / 3

    return int(cx), int(cy), int(radius)


def estimate_moon_rotation(img_array, cx, cy, radius):
    """Estimate moon rotation angle by analyzing feature distribution"""
    # Use a ring around the moon to detect features
    gray = np.mean(img_array[:, :, :3], axis=2)

    # Create polar coordinate system centered on moon
    height, width = gray.shape
    y, x = np.ogrid[:height, :width]

    # Calculate angle from center for each pixel
    dy = y - cy
    dx = x - cx
    angles = np.arctan2(dy, dx)
    distances = np.sqrt(dx**2 + dy**2)

    # Look at pixels in a ring (70-90% of radius) where features are visible
    mask = (distances > radius * 0.7) & (distances < radius * 0.9)

    if not np.any(mask):
        return 0.0  # No features detected

    # Find dominant feature orientation using gradient
    masked_gray = gray * mask

    # Simple rotation estimate based on brightest quadrant
    # Divide into 4 quadrants and find which has most brightness
    top = np.sum(masked_gray[cy - int(radius) : cy, :])
    bottom = np.sum(masked_gray[cy : cy + int(radius), :])
    left = np.sum(masked_gray[:, cx - int(radius) : cx])
    right = np.sum(masked_gray[:, cx : cx + int(radius)])

    # Calculate rough rotation angle based on brightness distribution
    horizontal_bias = (right - left) / (right + left + 1)
    vertical_bias = (bottom - top) / (bottom + top + 1)

    rotation_angle = np.degrees(np.arctan2(vertical_bias, horizontal_bias))

    return rotation_angle


def normalize_brightness(img_array, target_array):
    """Normalize brightness to match target image"""
    # Calculate mean brightness
    img_brightness = np.mean(img_array[:, :, :3])
    target_brightness = np.mean(target_array[:, :, :3])

    if img_brightness > 0:
        scale_factor = target_brightness / img_brightness
        # Apply brightness correction
        result = img_array.copy()
        result[:, :, :3] = np.clip(result[:, :, :3] * scale_factor, 0, 255)
        return result
    return img_array


def crop_and_scale_moon(img, center_x, center_y, radius, target_size=None):
    """Crop moon region and optionally scale to target size"""
    # Add padding around moon
    padding = int(radius * 0.1)
    crop_radius = radius + padding

    # Calculate desired crop size (square)
    crop_size = int(crop_radius * 2)

    # Calculate crop box centered on moon
    left = int(center_x - crop_size // 2)
    top = int(center_y - crop_size // 2)
    right = left + crop_size
    bottom = top + crop_size

    # Ensure within bounds while maintaining square
    if left < 0:
        shift = -left
        left = 0
        right = min(right + shift, img.width)
        crop_size = right - left
        bottom = top + crop_size
    if top < 0:
        shift = -top
        top = 0
        bottom = min(bottom + shift, img.height)
        crop_size = bottom - top
        right = left + crop_size
    if right > img.width:
        shift = right - img.width
        right = img.width
        left = max(0, left - shift)
        crop_size = right - left
        bottom = top + crop_size
    if bottom > img.height:
        shift = bottom - img.height
        bottom = img.height
        top = max(0, top - shift)
        crop_size = bottom - top
        right = left + crop_size

    # Final adjustment to ensure perfect square
    final_crop_size = min(right - left, bottom - top)
    right = left + final_crop_size
    bottom = top + final_crop_size

    # Crop to moon region
    cropped = img.crop((left, top, right, bottom))

    # Scale if target size specified
    if target_size:
        cropped = cropped.resize(target_size, Image.Resampling.LANCZOS)

    return cropped, (center_x - left, center_y - top)


def align_and_scale_image(img, reference_center, reference_radius, img_center, img_radius):
    """Align and scale image to match reference"""
    # Calculate scale factor
    scale = reference_radius / img_radius if img_radius > 0 else 1.0

    # Calculate translation
    tx = reference_center[0] - img_center[0] * scale
    ty = reference_center[1] - img_center[1] * scale

    # Resize if needed
    if abs(scale - 1.0) > 0.01:
        new_size = (int(img.width * scale), int(img.height * scale))
        img = img.resize(new_size, Image.Resampling.LANCZOS)

    # Create canvas for translation
    result = Image.new("RGBA", img.size, (0, 0, 0, 0))
    result.paste(img, (int(tx), int(ty)))

    return result


def process_single_phase(args):
    """Process a single phase image (for parallel processing)"""
    (
        i,
        day_num,
        moon_dir,
        full_array,
        new_array,
        ref_cx,
        ref_cy,
        ref_radius,
        scale_size,
    ) = args

    phase_img = Image.open(f"{moon_dir}/moon.{i:04d}.tif").convert("RGBA")

    phase_array = np.array(phase_img, dtype=np.float32)

    # Find phase image parameters
    phase_cx, phase_cy, phase_radius = find_moon_center_and_radius(phase_array)

    # Calculate astronomical rotation angle (libration) for this phase
    # Keep original orientation - rotation will be applied during reconstruction
    rotation_angle = calculate_lunar_libration_angle(i)  # i is hour index

    # Crop and scale moon region if scale specified
    if scale_size:
        phase_img, (phase_cx, phase_cy) = crop_and_scale_moon(phase_img, phase_cx, phase_cy, phase_radius, scale_size)
        # Recalculate after cropping
        phase_array = np.array(phase_img, dtype=np.float32)
        phase_cx, phase_cy, phase_radius = find_moon_center_and_radius(phase_array)

    # Center crop phase image to moon with same margin as reference
    target_height = full_array.shape[0]
    target_width = full_array.shape[1]

    # Calculate crop region centered on moon
    crop_margin = int(phase_radius * 1.2)
    left = max(0, phase_cx - crop_margin)
    top = max(0, phase_cy - crop_margin)
    right = min(phase_array.shape[1], phase_cx + crop_margin)
    bottom = min(phase_array.shape[0], phase_cy + crop_margin)

    # Crop phase array
    phase_array_cropped = phase_array[top:bottom, left:right]

    # Convert RGBA to RGB with black background
    if phase_array_cropped.shape[2] == 4:
        alpha = phase_array_cropped[:, :, 3:4] / 255.0
        phase_array = phase_array_cropped[:, :, :3] * alpha
    else:
        phase_array = phase_array_cropped[:, :, :3]

    # Store the original cropped image for comparison (before further processing)
    original_img = Image.fromarray(phase_array.astype(np.uint8), mode="RGB")

    # Resize to match reference dimensions if needed
    if phase_array.shape[:2] != (target_height, target_width):
        phase_img_resized = Image.fromarray(phase_array.astype(np.uint8), mode="RGB").resize(
            (target_width, target_height), Image.Resampling.LANCZOS
        )
        phase_array = np.array(phase_img_resized, dtype=np.float32)

    # Align phase to full moon using phase correlation for better feature matching
    full_gray = np.mean(full_array[:, :, :3], axis=2)
    phase_gray = np.mean(phase_array[:, :, :3], axis=2)
    shift, error, diffphase = phase_cross_correlation(full_gray, phase_gray, upsample_factor=10)
    dy, dx = shift
    if abs(dx) > 0.5 or abs(dy) > 0.5:  # Only shift if significant
        # Shift phase_array
        phase_array_shifted = np.zeros_like(phase_array)
        for c in range(phase_array.shape[2]):
            phase_array_shifted[:, :, c] = ndimage.shift(phase_array[:, :, c], (dy, dx), mode="constant", cval=0)
        phase_array = phase_array_shifted

    # Calculate alpha mask using the centered, cropped arrays
    height = phase_array.shape[0]
    width = phase_array.shape[1]

    alpha_mask = np.zeros((height, width), dtype=np.uint8)

    for y in range(height):
        for x in range(width):
            full_lum = np.mean(full_array[y, x, :3])
            new_lum = np.mean(new_array[y, x, :3])
            phase_lum = np.mean(phase_array[y, x, :3])

            if abs(full_lum - new_lum) > 5:
                alpha = (phase_lum - new_lum) / (full_lum - new_lum)
                alpha = np.clip(alpha, 0, 1)
            else:
                alpha = phase_lum / 255.0 if full_lum > 128 else 0.5

            alpha_mask[y, x] = int(alpha * 255)

    # Extract shadow detail with smoother gradients
    # Use the terminator region (transition between light and dark)
    phase_gray = np.mean(phase_array[:, :, :3], axis=2)

    # Detect shadow/terminator areas with gradient
    shadow_threshold_low = 30
    shadow_threshold_high = 80

    # Create smooth shadow mask using soft thresholding
    shadow_mask = np.clip(
        (shadow_threshold_high - phase_gray) / (shadow_threshold_high - shadow_threshold_low),
        0,
        1,
    )

    # Extract shadow detail texture with smooth falloff
    shadow_detail = np.zeros_like(phase_array[:, :, :3], dtype=np.float32)
    for c in range(3):
        shadow_detail[:, :, c] = phase_array[:, :, c] * shadow_mask

    return (
        day_num,
        i,
        alpha_mask,
        original_img,
        shadow_detail,
        rotation_angle,
        (phase_cx, phase_cy, phase_radius),
    )


def generate_alpha_masks(
    moon_dir,
    output_dir,
    full_moon_idx,
    new_moon_idx,
    interval,
    num_days,
    workers=None,
    scale_size=None,
):
    """Generate alpha masks from moon phase sequence with alignment and normalization"""

    console.print(Panel.fit("[bold cyan]🌙 Moon Phase Alpha Mask Generator", border_style="cyan"))

    if scale_size:
        console.print(f"[yellow]⚡ Fast mode: Scaling moon region to {scale_size[0]}x{scale_size[1]}")

    with console.status("[bold green]Loading reference images...") as status:
        # Load reference images
        full_moon = Image.open(f"{moon_dir}/moon.{full_moon_idx:04d}.tif").convert("RGBA")
        new_moon = Image.open(f"{moon_dir}/moon.{new_moon_idx:04d}.tif").convert("RGBA")

        full_array = np.array(full_moon, dtype=np.float32)
        new_array = np.array(new_moon, dtype=np.float32)

        # Save original images before any rotation (for reference)
        os.makedirs(output_dir, exist_ok=True)
        full_moon.save(f"{output_dir}/moon_full_original.png")
        new_moon.save(f"{output_dir}/moon_new_original.png")

        status.update("[bold green]Detecting moon parameters...")
        # Find reference parameters
        full_cx, full_cy, full_radius = find_moon_center_and_radius(full_array)
        new_cx, new_cy, new_radius = find_moon_center_and_radius(new_array)

        # Calculate astronomical rotation (libration) for both reference images
        status.update("[bold green]Calculating astronomical libration angles...")
        full_rotation = calculate_lunar_libration_angle(full_moon_idx)
        new_rotation = calculate_lunar_libration_angle(new_moon_idx)

        console.print(
            f"[cyan]Full moon libration: {full_rotation:.2f}° (hour {full_moon_idx}), New moon libration: {new_rotation:.2f}° (hour {new_moon_idx})"
        )

        # Use full moon orientation as zero reference - normalize both images to full moon's orientation
        status.update("[bold green]Normalizing base images to full moon orientation...")
        if abs(full_rotation) > 0.5:  # Only rotate if significant
            full_array = rotate_image(full_array, -full_rotation)
            console.print(f"[green]✓ Full moon normalized to 0° (reference orientation)")

        # Normalize new moon relative to full moon's orientation
        new_rotation_relative = new_rotation - full_rotation
        if abs(new_rotation_relative) > 0.5:  # Only rotate if significant
            new_array = rotate_image(new_array, -new_rotation_relative)
            console.print(
                f"[green]✓ New moon normalized relative to full moon (rotated by {-new_rotation_relative:.2f}°)"
            )

        # Store full moon rotation as the zero reference
        zero_reference_rotation = full_rotation

        # Recalculate parameters after rotation
        full_cx, full_cy, full_radius = find_moon_center_and_radius(full_array)
        new_cx, new_cy, new_radius = find_moon_center_and_radius(new_array)

        # Normalize moon sizes to the same radius
        target_radius = max(full_radius, new_radius)
        if abs(full_radius - new_radius) > 5:  # Threshold to avoid scaling for small differences
            if full_radius < target_radius:
                scale = target_radius / full_radius
                new_size = (int(full_moon.width * scale), int(full_moon.height * scale))
                full_moon = full_moon.resize(new_size, Image.Resampling.LANCZOS)
                full_array = np.array(full_moon, dtype=np.float32)
                full_cx, full_cy, full_radius = find_moon_center_and_radius(full_array)
                console.print(f"[green]✓ Full moon scaled up to match new moon radius ({target_radius}px)")
            if new_radius < target_radius:
                scale = target_radius / new_radius
                new_size = (int(new_moon.width * scale), int(new_moon.height * scale))
                new_moon = new_moon.resize(new_size, Image.Resampling.LANCZOS)
                new_array = np.array(new_moon, dtype=np.float32)
                new_cx, new_cy, new_radius = find_moon_center_and_radius(new_array)
                console.print(f"[green]✓ New moon scaled up to match full moon radius ({target_radius}px)")

        # Crop and scale reference images if scale specified
        if scale_size:
            status.update("[bold green]Scaling reference images...")
            full_moon, (full_cx, full_cy) = crop_and_scale_moon(full_moon, full_cx, full_cy, full_radius, scale_size)
            new_moon, (new_cx, new_cy) = crop_and_scale_moon(new_moon, new_cx, new_cy, new_radius, scale_size)

            # Recalculate parameters after scaling
            full_array = np.array(full_moon, dtype=np.float32)
            new_array = np.array(new_moon, dtype=np.float32)
            full_cx, full_cy, full_radius = find_moon_center_and_radius(full_array)
            new_cx, new_cy, new_radius = find_moon_center_and_radius(new_array)

        status.update("[bold green]Centering reference images...")
        # Crop both reference images to center on moon with 20% margin
        crop_margin = int(full_radius * 1.2)
        crop_size = crop_margin * 2

        # Crop full moon
        left = max(0, full_cx - crop_margin)
        top = max(0, full_cy - crop_margin)
        right = min(full_array.shape[1], full_cx + crop_margin)
        bottom = min(full_array.shape[0], full_cy + crop_margin)
        full_array_cropped = full_array[top:bottom, left:right]

        # Preserve alpha channel
        if full_array_cropped.shape[2] == 4:
            full_array = full_array_cropped  # Keep RGBA
        else:
            # If no alpha, add alpha channel of 255
            alpha_channel = np.full(
                (full_array_cropped.shape[0], full_array_cropped.shape[1], 1), 255, dtype=np.float32
            )
            full_array = np.concatenate([full_array_cropped, alpha_channel], axis=2)

        full_moon = Image.fromarray(full_array.astype(np.uint8), mode="RGBA")

        # Crop new moon to same size centered on its moon
        left = max(0, new_cx - crop_margin)
        top = max(0, new_cy - crop_margin)
        right = min(new_array.shape[1], new_cx + crop_margin)
        bottom = min(new_array.shape[0], new_cy + crop_margin)
        new_array_cropped = new_array[top:bottom, left:right]

        # Preserve alpha channel
        if new_array_cropped.shape[2] == 4:
            new_array = new_array_cropped  # Keep RGBA
        else:
            # If no alpha, add alpha channel of 255
            alpha_channel = np.full((new_array_cropped.shape[0], new_array_cropped.shape[1], 1), 255, dtype=np.float32)
            new_array = np.concatenate([new_array_cropped, alpha_channel], axis=2)

        new_moon = Image.fromarray(new_array.astype(np.uint8), mode="RGBA")

        # Ensure both arrays have the same dimensions for phase correlation
        if full_array.shape[:2] != new_array.shape[:2]:
            # Resize new moon to match full moon dimensions
            new_moon_resized = new_moon.resize((full_array.shape[1], full_array.shape[0]), Image.Resampling.LANCZOS)
            new_array = np.array(new_moon_resized, dtype=np.float32)
            console.print(
                f"[green]✓ Resized new moon to match full moon dimensions ({full_array.shape[1]}x{full_array.shape[0]})"
            )

        # Align new moon to full moon using phase correlation for better feature matching
        full_gray = np.mean(full_array[:, :, :3], axis=2)
        new_gray = np.mean(new_array[:, :, :3], axis=2)
        shift, error, diffphase = phase_cross_correlation(full_gray, new_gray, upsample_factor=10)
        dy, dx = shift
        if abs(dx) > 0.5 or abs(dy) > 0.5:  # Only shift if significant
            # Shift new_array
            new_array_shifted = np.zeros_like(new_array)
            for c in range(new_array.shape[2]):
                new_array_shifted[:, :, c] = ndimage.shift(new_array[:, :, c], (dy, dx), mode="constant", cval=0)
            new_array = new_array_shifted
            new_moon = Image.fromarray(new_array.astype(np.uint8), mode="RGBA")
            console.print(f"[green]✓ Aligned new moon to full moon with shift ({dx:.1f}, {dy:.1f}) pixels")

    # Display reference info
    ref_table = Table(title="Reference Images", box=box.ROUNDED)
    ref_table.add_column("Image", style="cyan")
    ref_table.add_column("Size", style="green")
    ref_table.add_row("Full Moon (centered)", f"{full_array.shape[1]}x{full_array.shape[0]}")
    ref_table.add_row("New Moon (centered)", f"{new_array.shape[1]}x{new_array.shape[0]}")
    console.print(ref_table)

    os.makedirs(output_dir, exist_ok=True)

    # Clean up existing files before starting
    console.print("\n[yellow]🧹 Cleaning up output directory...")
    cleaned_count = 0

    # Remove old mask files
    for f in os.listdir(output_dir):
        if f.startswith("moon_mask_") and f.endswith(".png"):
            os.remove(os.path.join(output_dir, f))
            cleaned_count += 1

    # Remove old base images
    for base_file in ["moon_full.png", "moon_new.png", "moon_shadow_detail.png"]:
        base_path = os.path.join(output_dir, base_file)
        if os.path.exists(base_path):
            os.remove(base_path)
            cleaned_count += 1

    # Remove comparison directory
    comparison_dir = os.path.join(output_dir, "comparison")
    if os.path.exists(comparison_dir):
        import shutil

        shutil.rmtree(comparison_dir)
        cleaned_count += 1

    # Remove comparison video
    video_path = os.path.join(output_dir, "moon_comparison.mp4")
    if os.path.exists(video_path):
        os.remove(video_path)
        cleaned_count += 1

    if cleaned_count > 0:
        console.print(f"[green]✓ Cleaned {cleaned_count} old file(s)/directory")
    else:
        console.print("[green]✓ Directory already clean")

    # Prepare processing tasks
    console.print(f"\n[bold cyan]Generating alpha masks (1 every {interval} hours)...")
    total_hours = int(num_days * 24)
    phase_indices = list(range(1, total_hours + 1, interval))
    tasks = [
        (
            i,
            ((i - 1) // 24) + 1,
            moon_dir,
            full_array,
            new_array,
            0,  # ref_cx not needed anymore
            0,  # ref_cy not needed anymore
            0,  # ref_radius not needed anymore
            scale_size,
        )
        for i in phase_indices
    ]

    original_images = []
    mask_indices = []
    shadow_details = []  # Collect shadow detail from each phase
    all_rotation_angles = []  # Collect all rotation angles to calculate average

    # Process in parallel with thread pool
    num_workers = workers or os.cpu_count()

    console.print(f"[cyan]Using {num_workers} worker threads for parallel processing")

    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
        TextColumn("({task.completed}/{task.total})"),
        TimeRemainingColumn(),
        console=console,
    ) as progress:
        task = progress.add_task("[cyan]Processing moon phases...", total=len(tasks))

        with ThreadPoolExecutor(max_workers=num_workers) as executor:
            for result in executor.map(process_single_phase, tasks):
                (
                    day_num,
                    hour_idx,
                    alpha_mask,
                    original_img,
                    shadow_detail,
                    rotation_angle,
                    (cx, cy, radius),
                ) = result

                # Save alpha mask
                mask_img = Image.fromarray(alpha_mask, mode="L")
                mask_img.save(f"{output_dir}/moon_mask_{hour_idx:04d}.png")

                original_images.append((hour_idx, original_img))
                mask_indices.append((day_num, hour_idx))

                # Collect shadow detail from this phase
                shadow_details.append(shadow_detail)
                all_rotation_angles.append(rotation_angle)

                hour_of_day = hour_idx % 24
                progress.update(
                    task,
                    advance=1,
                    description=f"[cyan]Processing... Day {day_num} {hour_of_day:02d}:00 complete [dim](center: {cx},{cy}, radius: {radius}px)",
                )

    # Save base images
    console.print("\n[bold green]Saving base images...")
    full_moon.save(f"{output_dir}/moon_full.png")
    new_moon.save(f"{output_dir}/moon_new.png")

    # Combine shadow details from all phases - use maximum value at each pixel
    console.print("[bold green]Combining shadow details...")
    combined_shadow = np.zeros_like(shadow_details[0], dtype=np.float32)
    for shadow in shadow_details:
        combined_shadow = np.maximum(combined_shadow, shadow)

    # Save combined shadow detail with alpha channel preserved
    shadow_img = Image.fromarray(combined_shadow.astype(np.uint8), mode="RGB")
    shadow_rgba = Image.new("RGBA", shadow_img.size)
    shadow_rgba.paste(shadow_img)
    shadow_rgba.save(f"{output_dir}/moon_shadow_detail.png")

    console.print(f"[green]✓ Saved moon_full.png, moon_new.png, and moon_shadow_detail.png")
    console.print(f"[green]✓ Saved {len(mask_indices)} alpha masks")

    # Store rotation angles for each phase (for reconstruction)
    console.print(
        f"[cyan]Libration range: {min(all_rotation_angles):.2f}° to {max(all_rotation_angles):.2f}° (relative to full moon at 0°)"
    )

    return (
        full_array,
        new_array,
        combined_shadow,
        mask_indices,
        original_images,
        all_rotation_angles,
        zero_reference_rotation,
        (full_moon_idx, new_moon_idx),
    )


def reconstruct_from_mask(full_array, new_array, shadow_array, mask_img, rotation_angle, zero_reference):
    """Reconstruct moon phase from base images, shadow detail, and alpha mask

    Args:
        full_array: Normalized full moon base image
        new_array: Normalized new moon base image
        shadow_array: Combined shadow detail
        mask_img: Alpha mask for this phase
        rotation_angle: Astronomical libration angle for this phase
        zero_reference: Reference rotation (full moon's libration angle)
    """
    # Ensure mask matches reference dimensions
    target_height, target_width = full_array.shape[:2]
    if mask_img.size != (target_width, target_height):
        mask_img = mask_img.resize((target_width, target_height), Image.Resampling.LANCZOS)

    mask_array = np.array(mask_img, dtype=np.float32) / 255.0

    # Ensure all arrays match dimensions
    if new_array.shape[:2] != (target_height, target_width):
        new_img = Image.fromarray(new_array[:, :, :3].astype(np.uint8))
        new_img = new_img.resize((target_width, target_height), Image.Resampling.LANCZOS)
        new_array = np.array(new_img, dtype=np.float32)

    # Keep base images and mask at rotation 0, apply mask to unrotated images
    full_array_base = full_array[:, :, :3]
    new_array_base = new_array[:, :, :3]
    shadow_array_base = shadow_array[:, :, :3]
    mask_3d = np.stack([mask_array] * 3, axis=2)

    # Blend: phase = full * alpha + new * (1 - alpha)
    base_blend = full_array_base * mask_3d + new_array_base * (1 - mask_3d)

    # Apply shadow detail only where shadows exist and blend smoothly
    # Shadow detail should darken, not brighten or replace
    shadow_strength = np.mean(shadow_array_base, axis=2, keepdims=True) / 255.0
    shadow_strength = np.repeat(shadow_strength, 3, axis=2)

    # Use minimum to only darken, weighted by shadow strength
    # This prevents artifacts by only applying shadows where they naturally occur
    shadow_darkening = np.minimum(base_blend, shadow_array_base)
    reconstructed = base_blend * (1 - shadow_strength * 0.3) + shadow_darkening * (shadow_strength * 0.3)

    reconstructed = np.clip(reconstructed, 0, 255).astype(np.uint8)

    # Convert to PIL Image for rotation
    reconstructed_img = Image.fromarray(reconstructed, mode="RGB")

    # Apply libration rotation to the final reconstructed image
    if abs(rotation_angle) > 0.5:  # Only rotate if significant
        reconstructed_array = np.array(reconstructed_img)
        reconstructed_array = rotate_image(reconstructed_array, rotation_angle)
        reconstructed_img = Image.fromarray(reconstructed_array.astype(np.uint8), mode="RGB")

    return reconstructed_img


def calculate_similarity_score(img1, img2):
    """Calculate similarity score between two images (0-100%)"""
    # Convert to numpy arrays
    arr1 = np.array(img1, dtype=np.float32)
    arr2 = np.array(img2, dtype=np.float32)

    # Calculate mean absolute error per pixel
    mae = np.mean(np.abs(arr1 - arr2))

    # Convert to similarity percentage (0 = identical, 255 = max difference)
    # Similarity = 100% when mae = 0, 0% when mae = 255
    similarity = max(0, min(100, 100 * (1 - mae / 255)))

    return similarity


def colorize_mask_with_shadow(mask_img, shadow_array):
    """Convert grayscale mask to colorized version with shadow detail as blue channel"""
    mask_array = np.array(mask_img)

    # Ensure shadow array matches mask dimensions
    target_height, target_width = mask_array.shape[:2]
    if shadow_array.shape[:2] != (target_height, target_width):
        shadow_img = Image.fromarray(shadow_array[:, :, :3].astype(np.uint8), mode="RGB")
        shadow_img_resized = shadow_img.resize((target_width, target_height), Image.Resampling.LANCZOS)
        shadow_array_resized = np.array(shadow_img_resized, dtype=np.float32)
    else:
        shadow_array_resized = shadow_array

    # Create RGB image
    colored = np.zeros((target_height, target_width, 3), dtype=np.uint8)

    # Red channel for low values (new moon), Green channel for high values (full moon)
    colored[:, :, 0] = 255 - mask_array  # Red decreases as alpha increases
    colored[:, :, 1] = mask_array  # Green increases as alpha increases

    # Blue channel for shadow detail - normalize to 0-255 range
    shadow_intensity = np.mean(shadow_array_resized[:, :, :3], axis=2)
    # Boost shadow visibility by scaling
    shadow_intensity = np.clip(shadow_intensity * 2, 0, 255)
    colored[:, :, 2] = shadow_intensity.astype(np.uint8)

    return Image.fromarray(colored, mode="RGB")


def colorize_mask(mask_img):
    """Convert grayscale mask to colorized version (red=0/new moon, green=255/full moon)"""
    mask_array = np.array(mask_img)

    # Create RGB image
    colored = np.zeros((mask_array.shape[0], mask_array.shape[1], 3), dtype=np.uint8)

    # Red channel for low values (new moon), Green channel for high values (full moon)
    colored[:, :, 0] = 255 - mask_array  # Red decreases as alpha increases
    colored[:, :, 1] = mask_array  # Green increases as alpha increases
    colored[:, :, 2] = 0  # No blue

    return Image.fromarray(colored, mode="RGB")


def back_test_and_create_movie(
    output_dir,
    full_array,
    new_array,
    shadow_array,
    mask_indices,
    original_images,
    rotation_angles,
    zero_reference,
):
    """Back-test masks by reconstructing images and create comparison movie

    Args:
        rotation_angles: List of rotation angles for each phase
        zero_reference: Reference rotation (full moon's libration angle)
    """

    console.print(Panel.fit("[bold magenta]🎬 Back-Testing & Movie Creation", border_style="magenta"))

    comparison_dir = os.path.join(output_dir, "comparison")
    os.makedirs(comparison_dir, exist_ok=True)

    frames = []
    similarity_scores = []

    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
        console=console,
    ) as progress:
        task = progress.add_task("[magenta]Creating comparison frames...", total=len(mask_indices))

        for idx, (day_num, hour_idx) in enumerate(mask_indices):
            # Load mask
            mask_img = Image.open(f"{output_dir}/moon_mask_{hour_idx:04d}.png")

            # Create colorized version of mask with shadow detail
            mask_colored = colorize_mask_with_shadow(mask_img, shadow_array)

            # Get rotation angle for this phase
            phase_rotation = rotation_angles[idx]

            # Reconstruct image with rotation applied to base images
            reconstructed = reconstruct_from_mask(
                full_array,
                new_array,
                shadow_array,
                mask_img,
                phase_rotation,
                zero_reference,
            )

            # Find original image
            original_img = None
            for idx, img in original_images:
                if idx == hour_idx:
                    original_img = img.convert("RGB")
                    break

            if original_img is None:
                continue

            # Resize original to match reconstructed if needed
            if original_img.size != reconstructed.size:
                original_img = original_img.resize(reconstructed.size, Image.Resampling.LANCZOS)

            # Resize mask to match
            if mask_colored.size != reconstructed.size:
                mask_colored = mask_colored.resize(reconstructed.size, Image.Resampling.LANCZOS)
                mask_img = mask_img.resize(reconstructed.size, Image.Resampling.LANCZOS)

            mask_array = np.array(mask_img, dtype=np.uint8)

            # Compute shadow intensity for overlay
            shadow_resized = shadow_array
            if shadow_array.shape[:2] != (reconstructed.size[1], reconstructed.size[0]):
                shadow_img = Image.fromarray(shadow_array[:, :, :3].astype(np.uint8), mode="RGB")
                shadow_img = shadow_img.resize(reconstructed.size, Image.Resampling.LANCZOS)
                shadow_resized = np.array(shadow_img, dtype=np.float32)
            shadow_intensity = np.mean(shadow_resized, axis=2)
            shadow_intensity = np.clip(shadow_intensity * 2, 0, 255).astype(np.uint8)

            # Create separate mask component images
            # Red overlay for new moon areas
            red_overlay = original_img.copy()
            red_array = np.zeros((original_img.height, original_img.width, 4), dtype=np.uint8)
            inv_mask = 255 - mask_array
            red_array[:, :, 0] = inv_mask  # Red channel
            red_array[:, :, 3] = inv_mask  # Alpha
            red_img = Image.fromarray(red_array, mode="RGBA")
            red_overlay.paste(red_img, (0, 0), red_img)

            # Green overlay for full moon areas
            green_overlay = original_img.copy()
            green_array = np.zeros((original_img.height, original_img.width, 4), dtype=np.uint8)
            green_array[:, :, 1] = mask_array  # Green channel
            green_array[:, :, 3] = mask_array  # Alpha
            green_img = Image.fromarray(green_array, mode="RGBA")
            green_overlay.paste(green_img, (0, 0), green_img)

            # Blue overlay for shadows
            blue_overlay = original_img.copy()
            blue_array = np.zeros((original_img.height, original_img.width, 4), dtype=np.uint8)
            # Boost shadow visibility by scaling and ensuring minimum brightness
            enhanced_shadow = np.clip(shadow_intensity * 3, 50, 255).astype(np.uint8)
            blue_array[:, :, 2] = enhanced_shadow  # Blue channel - brighter
            blue_array[:, :, 3] = np.clip(shadow_intensity * 2, 100, 255).astype(np.uint8)  # Alpha - more opaque
            blue_img = Image.fromarray(blue_array, mode="RGBA")
            blue_overlay.paste(blue_img, (0, 0), blue_img)

            # Calculate similarity score
            similarity = calculate_similarity_score(original_img, reconstructed)

            # Set default comparison image size to 1920x1080
            comp_width = 1920
            comp_height = 1080
            label_height = 120
            panel_width = (comp_width - 80) // 3
            panel_height = comp_height - label_height
            comparison = Image.new("RGB", (comp_width, comp_height), (0, 0, 0))

            def fit_and_center(img, panel_w, panel_h):
                # Preserve aspect ratio, fit inside panel, center
                img_w, img_h = img.size
                scale = min(panel_w / img_w, panel_h / img_h)
                new_w = int(img_w * scale)
                new_h = int(img_h * scale)
                img_resized = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
                panel = Image.new("RGB", (panel_w, panel_h), (0, 0, 0))
                offset_x = (panel_w - new_w) // 2
                offset_y = (panel_h - new_h) // 2
                panel.paste(img_resized, (offset_x, offset_y))
                return panel

            def create_stacked_mask_panel(red_img, green_img, blue_img, panel_w, panel_h):
                # Create a panel with three mask components stacked vertically
                sub_panel_height = panel_h // 3
                stacked_panel = Image.new("RGB", (panel_w, panel_h), (0, 0, 0))

                red_panel = fit_and_center(red_img, panel_w, sub_panel_height)
                green_panel = fit_and_center(green_img, panel_w, sub_panel_height)
                blue_panel = fit_and_center(blue_img, panel_w, sub_panel_height)

                stacked_panel.paste(green_panel, (0, 0))
                stacked_panel.paste(red_panel, (0, sub_panel_height))
                stacked_panel.paste(blue_panel, (0, sub_panel_height * 2))

                return stacked_panel

            orig_panel = fit_and_center(original_img, panel_width, panel_height)
            mask_panel = create_stacked_mask_panel(red_overlay, green_overlay, blue_overlay, panel_width, panel_height)
            recon_panel = fit_and_center(reconstructed, panel_width, panel_height)

            # Add images: original | mask | reconstructed
            comparison.paste(orig_panel, (0, label_height))
            comparison.paste(mask_panel, (panel_width + 40, label_height))
            comparison.paste(recon_panel, (panel_width * 2 + 80, label_height))

            # Add labels with font scaled to panel size
            draw = ImageDraw.Draw(comparison)
            try:
                font_size = max(12, panel_width // 12)
                font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", font_size)
            except:
                try:
                    font_size = max(12, panel_width // 12)
                    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", font_size)
                except:
                    font = ImageFont.load_default()

            # Center labels above each image
            hour_of_day = hour_idx % 24
            phase_rotation_rel = phase_rotation - zero_reference
            similarity_scores.append((phase_rotation_rel, similarity))
            text1 = f"Day {day_num} {hour_of_day:02d}:00"
            text2 = f"Mask (libration: {phase_rotation_rel:.1f}°)"
            text3 = "Reconstructed"

            # Get text bounding boxes for centering
            bbox1 = draw.textbbox((0, 0), text1, font=font)
            bbox2 = draw.textbbox((0, 0), text2, font=font)
            bbox3 = draw.textbbox((0, 0), text3, font=font)
            text1_width = bbox1[2] - bbox1[0]
            text2_width = bbox2[2] - bbox2[0]
            text3_width = bbox3[2] - bbox3[0]

            # Draw centered text above each panel
            draw.text(
                (panel_width // 2 - text1_width // 2, 20),
                text1,
                fill=(255, 255, 0),
                font=font,
            )
            draw.text(
                (panel_width + 40 + panel_width // 2 - text2_width // 2, 20),
                text2,
                fill=(255, 128, 255),
                font=font,
            )
            draw.text(
                (panel_width * 2 + 80 + panel_width // 2 - text3_width // 2, 20),
                text3,
                fill=(0, 255, 255),
                font=font,
            )

            # Save comparison frame
            frame_path = os.path.join(comparison_dir, f"frame_{day_num:03d}.png")
            comparison.save(frame_path)
            frames.append(frame_path)
            progress.update(task, advance=1)

    # Display similarity statistics
    if similarity_scores:
        # Extract similarities for overall stats
        similarities = [score for _, score in similarity_scores]
        avg_similarity = np.mean(similarities)
        min_similarity = np.min(similarities)
        max_similarity = np.max(similarities)

        stats_table = Table(title="Reconstruction Quality", box=box.ROUNDED)
        stats_table.add_column("Metric", style="cyan")
        stats_table.add_column("Score", style="green", justify="right")
        stats_table.add_row("Average Similarity", f"{avg_similarity:.2f}%")
        stats_table.add_row("Minimum Similarity", f"{min_similarity:.2f}%")
        stats_table.add_row("Maximum Similarity", f"{max_similarity:.2f}%")
        console.print("\n")
        console.print(stats_table)

        # Display detailed table for each libration angle
        detail_table = Table(title="Similarity by Libration Angle", box=box.DOUBLE_EDGE)
        detail_table.add_column("Libration (°)", style="cyan", justify="right")
        detail_table.add_column("Day", style="yellow", justify="right")
        detail_table.add_column("Hour", style="yellow", justify="right")
        detail_table.add_column("Similarity (%)", style="green", justify="right")

        # Sort by libration angle
        sorted_scores = sorted(similarity_scores, key=lambda x: x[0])

        for libration, similarity in sorted_scores:
            # Find corresponding day/hour for this libration angle
            day_hour = None
            for idx, (day_num, hour_idx) in enumerate(mask_indices):
                if abs(rotation_angles[idx] - zero_reference - libration) < 0.1:  # Match libration
                    hour_of_day = hour_idx % 24
                    day_hour = (day_num, hour_of_day)
                    break

            if day_hour:
                detail_table.add_row(f"{libration:.1f}", str(day_hour[0]), f"{day_hour[1]:02d}:00", f"{similarity:.2f}")

        console.print("\n")
        console.print(detail_table)

    # Create movie using ffmpeg
    console.print("\n[bold yellow]Creating comparison video...")
    movie_path = os.path.join(output_dir, "moon_comparison.mp4")
    try:
        with console.status("[bold yellow]Encoding video with ffmpeg..."):
            result = subprocess.run(
                [
                    "ffmpeg",
                    "-y",
                    "-framerate",
                    "2",  # 2 frames per second
                    "-pattern_type",
                    "glob",
                    "-i",
                    f"{comparison_dir}/frame_*.png",
                    "-c:v",
                    "libx264",
                    "-pix_fmt",
                    "yuv420p",
                    "-crf",
                    "18",
                    movie_path,
                ],
                check=True,
                capture_output=True,
            )
        console.print(f"[green]✓ Video created: [bold]{movie_path}")
    except subprocess.CalledProcessError as e:
        console.print(f"[red]✗ Error creating video: {e}")
        console.print("[yellow]Make sure ffmpeg is installed: sudo apt install ffmpeg")
    except FileNotFoundError:
        console.print("[red]✗ ffmpeg not found. Install with: sudo apt install ffmpeg")


def calculate_size_comparison(moon_dir, output_dir, num_masks, num_days):
    """Calculate and display size comparison"""
    console.print(Panel.fit("[bold blue]📊 Storage Analysis", border_style="blue"))

    # Calculate original size (TIFF files, one per day)
    original_size = 0
    for i in range(1, int(num_days * 24) + 1, 24):
        try:
            file_path = f"{moon_dir}/moon.{i:04d}.tif"
            original_size += os.path.getsize(file_path)
        except:
            pass

    # Calculate new size (base images + masks)
    new_size = 0
    new_size += os.path.getsize(f"{output_dir}/moon_full.png")
    new_size += os.path.getsize(f"{output_dir}/moon_new.png")

    # Count actual mask files (iterate through mask_indices from processing)
    mask_files = [f for f in os.listdir(output_dir) if f.startswith("moon_mask_") and f.endswith(".png")]
    for mask_file in mask_files:
        new_size += os.path.getsize(os.path.join(output_dir, mask_file))

    # Estimate after converting to SJPG
    estimated_sjpg = new_size * 0.1  # Rough estimate: SJPG is ~10% of PNG

    # Create comparison table
    table = Table(title="Storage Comparison", box=box.DOUBLE_EDGE)
    table.add_column("Format", style="cyan", justify="left")
    table.add_column("Size", style="yellow", justify="right")
    table.add_column("Compression", style="green", justify="right")
    table.add_column("Space Saved", style="magenta", justify="right")

    table.add_row(
        f"Original ({num_days:.2f} days, {int(num_days)} TIFF)",
        f"{original_size / (1024*1024):.2f} MB",
        "1.0x (baseline)",
        "-",
    )
    table.add_row(
        f"PNG (2 base + {num_masks} masks)",
        f"{new_size / (1024*1024):.2f} MB",
        f"{original_size / new_size:.2f}x",
        f"{(original_size - new_size) / (1024*1024):.2f} MB ({100 * (1 - new_size/original_size):.1f}%)",
    )
    table.add_row(
        "SJPG (estimated)",
        f"{estimated_sjpg / (1024*1024):.2f} MB",
        f"{original_size / estimated_sjpg:.2f}x",
        f"{(original_size - estimated_sjpg) / (1024*1024):.2f} MB ({100 * (1 - estimated_sjpg/original_size):.1f}%)",
        style="bold green",
    )

    console.print(table)


async def main(args):
    """Main execution function"""
    # Convert paths to absolute
    moon_dir = str(Path(args.input).resolve())
    output_dir = str(Path(args.output).resolve())

    console.rule("[bold cyan]🌙 Moon Phase Mask Generator", style="cyan")

    # Display astronomical information
    astro_table = Table(title="Lunar Cycle Information", box=box.ROUNDED)
    astro_table.add_column("Property", style="cyan")
    astro_table.add_column("Value", style="yellow")
    astro_table.add_row("Synodic Month (New Moon to New Moon)", f"{LUNAR_CYCLE_DAYS:.2f} days")
    astro_table.add_row("Libration Period (Apparent Wobble)", f"{LIBRATION_PERIOD_DAYS:.2f} days")
    astro_table.add_row("Maximum Libration", f"±{MAX_LIBRATION_DEGREES:.1f}°")
    astro_table.add_row(
        "Expected Phases in {0} days".format(args.days),
        f"{args.days / LUNAR_CYCLE_DAYS:.2f} cycles",
    )
    console.print(astro_table)
    console.print()

    # Auto-detect moon phases if not specified
    full_moon_idx, new_moon_idx = args.full_moon, args.new_moon
    if full_moon_idx is None or new_moon_idx is None:
        try:
            detected_new, detected_full = detect_moon_phase_extremes(moon_dir)
            if new_moon_idx is None:
                new_moon_idx = detected_new
            if full_moon_idx is None:
                full_moon_idx = detected_full
        except Exception as e:
            console.print(f"[red]✗ Auto-detection failed: {e}")
            if full_moon_idx is None:
                full_moon_idx = 2098  # Fallback
            if new_moon_idx is None:
                new_moon_idx = 310  # Fallback
            console.print(f"[yellow]Using fallback values: Full={full_moon_idx}, New={new_moon_idx}")

    # Display configuration
    config_table = Table(title="Configuration", box=box.ROUNDED)
    config_table.add_column("Parameter", style="cyan")
    config_table.add_column("Value", style="yellow")
    config_table.add_row("Input Directory", moon_dir)
    config_table.add_row("Output Directory", output_dir)
    config_table.add_row(
        "Full Moon Index",
        str(full_moon_idx) + (" (auto-detected)" if full_moon_idx else ""),
    )
    config_table.add_row(
        "New Moon Index",
        str(new_moon_idx) + (" (auto-detected)" if new_moon_idx else ""),
    )
    config_table.add_row("Interval (hours)", str(args.interval))
    config_table.add_row("Days to Process", str(args.days))
    config_table.add_row("Download Missing", "No" if args.no_download else "Yes")
    config_table.add_row("Create Video", "No" if args.no_video else "Yes")
    config_table.add_row("Worker Threads", str(args.workers or os.cpu_count()))
    if args.scale:
        config_table.add_row("Scale", args.scale, style="bold yellow")
    console.print(config_table)
    console.print()

    # Parse scale option
    scale_size = parse_scale_option(args.scale)
    if args.scale and scale_size:
        console.print(f"[cyan]📏 Moon will be scaled to: {scale_size[0]}x{scale_size[1]} pixels")

    # Determine required images (reference images + phase images)
    required_indices = [full_moon_idx, new_moon_idx] + list(range(1, int(args.days * 24) + 1, args.interval))

    # Download missing images
    if not args.no_download:
        await download_missing_images(moon_dir, required_indices)

    # Generate masks
    (
        full_array,
        new_array,
        shadow_array,
        mask_indices,
        original_images,
        rotation_angles,
        zero_reference,
        (full_idx, new_idx),
    ) = generate_alpha_masks(
        moon_dir,
        output_dir,
        full_moon_idx,
        new_moon_idx,
        args.interval,
        args.days,
        args.workers,
        scale_size,
    )

    # Display base images and command hint
    console.print("\n[bold cyan]📸 Base Images Saved:")
    base_table = Table(box=box.SIMPLE)
    base_table.add_column("Image", style="cyan")
    base_table.add_column("Index", style="yellow")
    base_table.add_column("File", style="green")
    base_table.add_row("Full Moon (original)", str(full_idx), f"{output_dir}/moon_full_original.png")
    base_table.add_row("New Moon (original)", str(new_idx), f"{output_dir}/moon_new_original.png")
    base_table.add_row("Full Moon (normalized)", str(full_idx), f"{output_dir}/moon_full.png")
    base_table.add_row("New Moon (normalized)", str(new_idx), f"{output_dir}/moon_new.png")
    base_table.add_row("Shadow Detail (combined)", "-", f"{output_dir}/moon_shadow_detail.png")
    console.print(base_table)

    # Show command to skip auto-detection next time
    if args.full_moon is None or args.new_moon is None:
        console.print("\n[bold green]💡 Tip: Skip auto-detection next time with:")
        scale_arg = f" --scale {args.scale}" if args.scale else ""
        console.print(
            f"[dim]python {sys.argv[0]} -i {args.input} -o {args.output} --full-moon {full_idx} --new-moon {new_idx} --interval {args.interval}{scale_arg}[/dim]"
        )
        console.print()

    # Back-test and create movie
    if not args.no_video:
        back_test_and_create_movie(
            output_dir,
            full_array,
            new_array,
            shadow_array,
            mask_indices,
            original_images,
            rotation_angles,
            zero_reference,
        )
    else:
        console.print("[yellow]⚠ Skipping video creation (--no-video specified)")

    # Size comparison
    calculate_size_comparison(moon_dir, output_dir, len(mask_indices), args.days)

    console.rule("[bold green]✓ Processing Complete!", style="green")


if __name__ == "__main__":
    try:
        args = parse_arguments()
        asyncio.run(main(args))
    except KeyboardInterrupt:
        console.print("\n[yellow]⚠ Interrupted by user")
    except Exception as e:
        console.print(f"\n[red]✗ Error: {e}")
        raise
