// js/gpuseam.js: @kama/gpu's page half, linked into a program built for the web (`jsLibraries`): what the seam asks the
// page that webgpu.h cannot (AGENTS.gpu.md item 11).
//
// A release build runs Closure over this file (kama 0.9.966, KR-289), which renames every name it cannot prove the
// browser's. A name the browser reads keeps its own: WebGPU's methods are in Closure's declarations, and a dictionary's
// keys are quoted. These are emdawnwebgpu's, inside the program, which Closure renames alike everywhere, and the gate
// (tools/closure-names.mjs) holds every other name to its release build:
// closure-own: getJsObject TextureFormat

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

  // The canvas formats this browser refused, as WebGPU names them; null until a device has been granted. Learned once
  // a page: a browser's canvas formats are its own, not a device's.
  $gpuseamCanvasRefused: null,

  // Try WebGPU's three canvas formats on a canvas of the seam's own, never put in the page, with the device just
  // granted. WebGPU says every canvas takes all three, and has no question for it, but Firefox 160 refuses RGBA16Float by
  // throwing (its bug 1834395). A refusal by a validation error rather than a throw is a refusal too, and its scope keeps
  // it out of the program's channel.
  gpuseam_web_learn_canvas_formats__deps: ['$WebGPU', '$gpuseamCanvasRefused'],
  gpuseam_web_learn_canvas_formats: function (devicePtr) {
    if (gpuseamCanvasRefused !== null || typeof document === 'undefined') return;
    var device = WebGPU.getJsObject(devicePtr);
    if (!device) return;
    var context = null;
    try {
      var canvas = document.createElement('canvas');
      canvas.width = 1;
      canvas.height = 1;
      context = canvas.getContext('webgpu');
    } catch (err) { context = null; }
    if (!context) return;
    var refused = [];
    ['bgra8unorm', 'rgba8unorm', 'rgba16float'].forEach(function (format) {
      device.pushErrorScope('validation');
      var threw = false;
      try {
        // The dictionary's keys are quoted, as emdawnwebgpu's are, and the usage is GPUTextureUsage.RENDER_ATTACHMENT's
        // value: a release build's Closure has no declaration of the `GPUTextureUsage` namespace, so naming it fails the
        // build.
        context.configure({ 'device': device, 'format': format, 'usage': 0x10, 'alphaMode': 'opaque' });
      } catch (err) { threw = true; }
      device.popErrorScope().then(function (error) {
        if (error && refused.indexOf(format) < 0) refused.push(format);
      }, function () { });
      if (threw) refused.push(format);
    });
    try { context.unconfigure(); } catch (err) { }
    gpuseamCanvasRefused = refused;
  },

  // 0 when the browser refused `format` (a WGPUTextureFormat) for a canvas, else 1, and 1 before anything is learned.
  gpuseam_web_canvas_takes__deps: ['$WebGPU', '$gpuseamCanvasRefused'],
  gpuseam_web_canvas_takes: function (format) {
    if (gpuseamCanvasRefused === null) return 1;
    return gpuseamCanvasRefused.indexOf(WebGPU.TextureFormat[format]) < 0 ? 1 : 0;
  },

  // emdawnwebgpu's `wgpuSurfaceConfigure`, with what it throws caught: -1 when the canvas is configured, else the length
  // of the browser's words, written into `words` (at most `capacity` - 1 bytes, terminated). A browser's refusal is the
  // program's to handle, never its end.
  gpuseam_web_surface_configure__deps: ['wgpuSurfaceConfigure', '$stringToUTF8'],
  gpuseam_web_surface_configure: function (surface, config, words, capacity) {
    try {
      _wgpuSurfaceConfigure(surface, config);
      return -1;
    } catch (err) {
      var text = (err && err.message) ? String(err.message) : String(err);
      if (!text) text = 'the browser refused the configure';
      return stringToUTF8(text, words, capacity);
    }
  },
});
