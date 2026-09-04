//! Headless timing of the per-frame ntsc path, isolated from GL/JUCE, so the
//! rayon thread-count trade-off can be compared cleanly.
//!
//!   RAYON_NUM_THREADS=1 cargo run --release --example bench
//!   RAYON_NUM_THREADS=2 cargo run --release --example bench
//!
//! Set the pool size via RAYON_NUM_THREADS (the global pool reads it at first
//! use). ntscrs_new() only forces 2 when the var is UNSET, so an explicit value
//! always wins. Args: [w h frames [settings-json]]; the optional 4th arg is an
//! ntsc-rs settings JSON — use it to cost individual filter passes.

use std::time::Instant;

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let w: i32 = a.get(1).and_then(|s| s.parse().ok()).unwrap_or(640);
    let h: i32 = a.get(2).and_then(|s| s.parse().ok()).unwrap_or(540);
    let frames: i32 = a.get(3).and_then(|s| s.parse().ok()).unwrap_or(600);

    let threads = std::env::var("RAYON_NUM_THREADS").unwrap_or_else(|_| "unset".into());
    let mut rgba = vec![0u8; (w as usize) * (h as usize) * 4];
    for (i, px) in rgba.chunks_exact_mut(4).enumerate() {
        px[0] = (i % 256) as u8;
        px[1] = ((i / 256) % 256) as u8;
        px[2] = 128;
        px[3] = 255;
    }

    let handle = ntsc_ffi::ntscrs_new();
    // default: noise on so the filter path is exercised like the app (ntsc-noise 8)
    let json_str = a
        .get(4)
        .cloned()
        .unwrap_or_else(|| "{\"snow_intensity\":0.02}".to_string());
    let json = std::ffi::CString::new(json_str).unwrap();
    if unsafe { ntsc_ffi::ntscrs_load_json(handle, json.as_ptr()) } != 0 {
        eprintln!("bad settings JSON");
        std::process::exit(1);
    }

    // warm: first frame allocates scratch + inits the pool
    for f in 0..10 {
        unsafe { ntsc_ffi::ntscrs_process(handle, rgba.as_mut_ptr(), w, h, w * 4, f, 1) };
    }

    let t0 = Instant::now();
    for f in 0..frames {
        unsafe { ntsc_ffi::ntscrs_process(handle, rgba.as_mut_ptr(), w, h, w * 4, f, 1) };
    }
    let dt = t0.elapsed();
    unsafe { ntsc_ffi::ntscrs_free(handle) };

    let ms = dt.as_secs_f64() * 1000.0 / frames as f64;
    println!(
        "threads={:>5}  {}x{}  {} frames  {:.3} ms/frame  {:.0} fps-headroom",
        threads,
        w,
        h,
        frames,
        ms,
        1000.0 / ms
    );
}
