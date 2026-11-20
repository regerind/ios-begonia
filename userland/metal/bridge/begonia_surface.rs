// begonia: a no-copy buffer over an IOSurface's pages that the GPU reaches in place. PanVK on this kernel imports no
// host pointer (the kernel maps no user pages into the Mali's address space), so every no-copy buffer was a buffer of
// the runtime's own memory that copies the caller's bytes in and out round each command buffer (host_sync.rs), and
// every texture over one a shadow image filled again before each encoder: the window server on Metal copied each
// surface it composited twice a frame and the frame it drew once more (y204-y208: 4 frames a second). The kernel does
// make a GPU buffer of an IOSurface (the request the GL path's Panfrost uses), and the transport gives that a
// descriptor PanVK imports as a dma-buf (hakios-native-surface-fd.c). This call hands the runtime such descriptors
// as its dma-buf source (HostMemory::with_dma_buf): PanVK refuses the pointer, the runtime asks the source.
// Included at the end of NativeMetalObjects.rs by tools/build_metal2vulkan_linux.sh, beside its helpers.

/// The transport's `hakios_native_surface_fd_create`: a descriptor of `size` bytes standing for the surface, and the
/// token that keeps its identity; negative on failure.
pub type NmbSurfaceFdCreate = unsafe extern "C" fn(surface: u32, size: u64, token: *mut u64) -> std::ffi::c_int;
/// The transport's `hakios_native_surface_fd_release`.
pub type NmbSurfaceFdRelease = unsafe extern "C" fn(token: u64) -> std::ffi::c_int;

/// What a surface's source keeps: the descriptors' tokens, released when the runtime drops the source (after the
/// imports it made of them went).
struct SurfaceDescriptors {
    create: NmbSurfaceFdCreate,
    release: NmbSurfaceFdRelease,
    surface: u32,
    base: usize,
    bytes: u64,
    tokens: std::sync::Mutex<Vec<u64>>,
}

impl Drop for SurfaceDescriptors {
    fn drop(&mut self) {
        let tokens = std::mem::take(&mut *self.tokens.lock().unwrap_or_else(|poisoned| poisoned.into_inner()));
        for token in tokens {
            // SAFETY: the transport's own token, released once.
            unsafe { (self.release)(token) };
        }
    }
}

static SURFACE_DESCRIPTORS: [std::sync::atomic::AtomicU64; 3] = [const { std::sync::atomic::AtomicU64::new(0) }; 3]; // made, refused, not from the first page

/// Descriptors the surface sources made, those the transport refused, and requests that did not start at the
/// surface's first page (which no descriptor serves: a descriptor is the whole surface from its start).
#[unsafe(no_mangle)]
pub extern "C" fn nmb_surface_descriptor_counts(out: *mut u64) -> bool {
    guard(false, || {
        if out.is_null() {
            return Err("null counts".into());
        }
        // SAFETY: the header's three words.
        let out = unsafe { std::slice::from_raw_parts_mut(out, 3) };
        for (word, count) in out.iter_mut().zip(&SURFACE_DESCRIPTORS) {
            *word = count.load(std::sync::atomic::Ordering::Relaxed);
        }
        Ok(true)
    })
}

/// `nmb_buffer_new_no_copy_releasing` over the `length` bytes at `pointer` that are IOSurface `surface`'s pages from
/// its first: where the device refuses the pointer, the runtime imports descriptors of the surface instead.
///
/// # Safety
/// As `nmb_buffer_new_no_copy_releasing`; `pointer` maps the surface's memory from its first page, and the surface is
/// retained until `release` runs.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nmb_buffer_new_no_copy_surface(device: *mut NmbDevice, pointer: *mut c_void, length: u64, options_word: u64,
                                                        release: Option<NmbRelease>, context: *mut c_void, surface: u32,
                                                        fd_create: Option<NmbSurfaceFdCreate>, fd_release: Option<NmbSurfaceFdRelease>)
                                                        -> *mut NmbBuffer {
    guard(std::ptr::null_mut(), || {
        use std::os::fd::FromRawFd;
        use std::sync::atomic::Ordering::Relaxed;
        let device = get(device)?;
        let (Some(create), Some(fd_release)) = (fd_create, fd_release) else { return Err("no surface descriptors".into()) };
        if surface == 0 {
            return Err("no surface".into());
        }
        let pointer = NonNull::new(pointer as *mut u8).ok_or("null memory")?;
        let modes = options(options_word)?;
        let descriptors = std::sync::Arc::new(SurfaceDescriptors {
            create,
            release: fd_release,
            surface,
            base: pointer.as_ptr() as usize,
            bytes: length,
            tokens: std::sync::Mutex::new(Vec::new()),
        });
        let source: metal2vulkan_runtime::DmaBufSource = std::sync::Arc::new(move |request: metal2vulkan_runtime::DmaBufRequest| {
            let refused = |why: &'static str| std::io::Error::new(std::io::ErrorKind::Unsupported, why);
            if request.start.as_ptr() as usize != descriptors.base || request.length == 0 || request.length > descriptors.bytes {
                SURFACE_DESCRIPTORS[2].fetch_add(1, Relaxed);
                return Err(refused("not the surface's pages from its first"));
            }
            let mut token = 0u64;
            // SAFETY: the transport's call; the token is written before a descriptor is returned.
            let fd = unsafe { (descriptors.create)(descriptors.surface, request.length, &mut token) };
            if fd < 0 || token == 0 {
                SURFACE_DESCRIPTORS[1].fetch_add(1, Relaxed);
                return Err(refused("the transport made no descriptor of the surface"));
            }
            descriptors.tokens.lock().unwrap_or_else(|poisoned| poisoned.into_inner()).push(token);
            SURFACE_DESCRIPTORS[0].fetch_add(1, Relaxed);
            // SAFETY: a new descriptor, this call's alone.
            let fd = unsafe { std::os::fd::OwnedFd::from_raw_fd(fd) };
            Ok(vec![metal2vulkan_runtime::DmaBufChunk { fd, length: request.length }])
        });
        let deallocator = release.map(|release| {
            let context = ReleaseContext(context);
            Box::new(move |pointer: NonNull<u8>, length: u64| {
                let context = context;
                // SAFETY: the caller's contract.
                unsafe { release(context.0, pointer.as_ptr() as *mut c_void, length) }
            }) as Deallocator
        });
        // SAFETY: the caller's contract: the bytes are the surface's pages, which the descriptors stand for.
        let memory = unsafe { HostMemory::with_dma_buf(pointer, source) };
        let buffer = {
            let _gate = device.2.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
            // SAFETY: as above.
            unsafe { device.0.new_buffer_with_bytes_no_copy(memory, length, modes, deallocator) }
        };
        Ok(boxed(NmbBuffer(buffer.map_err(e("surface buffer"))?)))
    })
}
