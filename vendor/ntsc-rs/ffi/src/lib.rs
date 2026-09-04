//! C FFI for the ntsc-rs core effect, consumed by app/NTSCEffect.cpp.
//!
//! Per-frame path is the efficient 3-call sequence (NOT the allocating
//! `apply_effect_to_buffer` convenience, which heap-allocates the whole YIQ
//! plane every call): set_from_strided_buffer -> apply_effect_to_yiq ->
//! write_to_strided_buffer, with the f32 scratch buffer reused across frames.

use std::ffi::CStr;
use std::os::raw::{c_char, c_int};

use ntsc_rs::ctx::Context;
use ntsc_rs::settings::{SettingsList, UseField};
use ntsc_rs::yiq_fielding::{BlitInfo, DeinterlaceMode, Rect, Rgbx, YiqView};
use ntsc_rs::NtscEffect;

pub struct Handle {
    ctx: Context,
    effect: NtscEffect,
    settings_list: SettingsList<NtscEffect>,
    scratch: Vec<f32>,
}

fn default_effect() -> NtscEffect {
    let mut e = NtscEffect::default();
    // Upper field + Bob deinterlace: half the rows (~half the cost) and the
    // right reconstruction for a 30 fps progressive GL app.
    e.use_field = UseField::Upper;
    e
}

/// Create an effect instance (Context + default settings + scratch).
///
/// The rayon pool defaults to ALL physical cores; force 1 thread. With a
/// single thread ntsc-rs's ZipChunks::par_for_each takes the seq_for_each
/// path INLINE on the caller — no pool fan-out at all. That matters more than
/// the filter math: apply_effect_to_yiq is dozens of row-passes, each a
/// rayon scope() fan-out/join, and at 25 fps the wake/park churn across
/// workers measured ~24 CPU points on the whole app (44% with 2 threads vs
/// 20% with 1, same sketch/build, `top` true-interval) — dwarfing the filter
/// itself (~10 points single-threaded; headless: 3.9 ms/frame vs a 40 ms
/// frame budget). Latency only matters once a sketch is heavy enough to blow
/// the budget — then raise RAYON_NUM_THREADS (bench: 2t = 2.1 ms, 4t = 1.4).
/// Note `open`-launched .apps drop shell env (launchd), so this in-process
/// default is what a bundled app gets; the override needs a direct launch.
#[no_mangle]
pub extern "C" fn ntscrs_new() -> *mut Handle {
    if std::env::var_os("RAYON_NUM_THREADS").is_none() {
        std::env::set_var("RAYON_NUM_THREADS", "1");
    }
    Box::into_raw(Box::new(Handle {
        ctx: Context::new(),
        effect: default_effect(),
        settings_list: SettingsList::new(),
        scratch: Vec::new(),
    }))
}

/// # Safety
/// `h` must be a pointer returned by `ntscrs_new` (or null).
#[no_mangle]
pub unsafe extern "C" fn ntscrs_free(h: *mut Handle) {
    if !h.is_null() {
        drop(Box::from_raw(h));
    }
}

/// Load a settings preset from ntsc-rs / ntscQT JSON. NULL or empty string
/// resets to the default settings. Returns 0 on success, 1 on parse error.
///
/// # Safety
/// `h` from `ntscrs_new`; `json` a NUL-terminated string or null.
#[no_mangle]
pub unsafe extern "C" fn ntscrs_load_json(h: *mut Handle, json: *const c_char) -> c_int {
    let h = &mut *h;
    let s = if json.is_null() {
        String::new()
    } else {
        CStr::from_ptr(json).to_string_lossy().into_owned()
    };
    let s = s.trim();
    if s.is_empty() {
        h.effect = default_effect();
        return 0;
    }
    match h.settings_list.from_json(s) {
        Ok(e) => {
            h.effect = e;
            0
        }
        Err(_) => 1,
    }
}

/// Run the effect in place on an RGBA8 buffer. `row_bytes` = bytes per row
/// (>= w*4); `flip_y` nonzero for bottom-up buffers (glReadPixels). Alpha is
/// overwritten with 255 by ntsc-rs. `frame_num` drives noise/phase animation —
/// increment it every frame.
///
/// # Safety
/// `h` from `ntscrs_new`; `rgba` must point to `row_bytes * hgt` valid bytes.
#[no_mangle]
pub unsafe extern "C" fn ntscrs_process(
    h: *mut Handle,
    rgba: *mut u8,
    w: c_int,
    hgt: c_int,
    row_bytes: c_int,
    frame_num: c_int,
    flip_y: c_int,
) {
    let h = &mut *h;
    let (w, hh) = (w as usize, hgt as usize);
    if w == 0 || hh == 0 {
        return;
    }
    let rb = row_bytes as usize;
    let buf = std::slice::from_raw_parts_mut(rgba, rb * hh);

    let dims = (w, hh);
    let needed = YiqView::max_buf_length_for(dims, h.effect.use_field);
    if h.scratch.len() < needed {
        h.scratch = vec![0f32; needed];
    }

    let frame = frame_num as usize;
    let field = h.effect.use_field.to_yiq_field(frame);
    let len = YiqView::buf_length_for(dims, field);
    let mut yiq = YiqView::from_parts(&mut h.scratch[..len], dims, field);

    let blit = BlitInfo::new(Rect::new(0, 0, hh, w), (0, 0), rb, hh, flip_y != 0);
    yiq.set_from_strided_buffer::<Rgbx, u8, _>(&h.ctx, buf, blit, ());
    h.effect.apply_effect_to_yiq(&h.ctx, &mut yiq, frame, [1.0, 1.0]);
    yiq.write_to_strided_buffer::<Rgbx, u8, _>(&h.ctx, buf, blit, DeinterlaceMode::Bob, ());
}
