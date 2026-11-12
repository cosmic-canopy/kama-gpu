// js/gpuseam.js: @kama/gpu's page half, linked into a program built for the web (`jsLibraries`): what the seam asks the
// page that webgpu.h cannot (AGENTS.gpu.md item 11).

mergeInto(LibraryManager.library, {
  // 1 when the canvas `selector` names gives a WebGPU context, else 0. emdawnwebgpu's `wgpuInstanceCreateSurface`
  // asserts the context in a debug build, ending the program, so the seam asks first and refuses the surface itself.
  // A browser without WebGPU gives none to any canvas (headless Firefox 160 here), and a canvas holding another
  // context, a 2D one, gives none. A canvas asked twice gives the same context, so asking first changes nothing.
  gpuseam_web_canvas_has_webgpu__deps: ['$UTF8ToString'],
  gpuseam_web_canvas_has_webgpu: function (selector) {
    var el = null;
    try { el = document.querySelector(UTF8ToString(selector)); } catch (err) { el = null; }
    if (!el || typeof el.getContext !== 'function') return 0;
    var context = null;
    try { context = el.getContext('webgpu'); } catch (err) { context = null; }
    return context ? 1 : 0;
  },
});
