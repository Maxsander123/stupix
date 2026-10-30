/*
 * GPU: inventory (sysfs DRM, any vendor), NVIDIA sensors (NVML) and a compute
 * benchmark through OpenCL. Both libraries are loaded at runtime from the
 * installed driver; nothing is needed at build time.
 */
#include "stx.h"

#include <stdlib.h>
#include <string.h>

/* reference: NVIDIA RTX A500 Laptop GPU (bandwidth/transfer refs are its nominal values) */
#define REF_GPU_FLOPS 4200.0 /* GFLOPS fp32 */
#define REF_GPU_BW 100.0     /* GB/s device memory */
#define REF_GPU_H2D 10.0     /* GB/s host -> device */

/* ---- minimal OpenCL ABI ---- */
typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef uint64_t cl_bitfield;
typedef void *cl_platform_id, *cl_device_id, *cl_context, *cl_command_queue, *cl_mem, *cl_program, *cl_kernel,
    *cl_event;
typedef intptr_t cl_context_properties;

#ifdef STX_OS_WINDOWS
#define CLAPI __stdcall
#else
#define CLAPI
#endif

static struct {
    cl_int(CLAPI *GetPlatformIDs)(cl_uint, cl_platform_id *, cl_uint *);
    cl_int(CLAPI *GetDeviceIDs)(cl_platform_id, cl_bitfield, cl_uint, cl_device_id *, cl_uint *);
    cl_int(CLAPI *GetDeviceInfo)(cl_device_id, cl_uint, size_t, void *, size_t *);
    cl_context(CLAPI *CreateContext)(const cl_context_properties *, cl_uint, const cl_device_id *, void *, void *,
                                     cl_int *);
    cl_command_queue(CLAPI *CreateCommandQueue)(cl_context, cl_device_id, cl_bitfield, cl_int *);
    cl_program(CLAPI *CreateProgramWithSource)(cl_context, cl_uint, const char **, const size_t *, cl_int *);
    cl_int(CLAPI *BuildProgram)(cl_program, cl_uint, const cl_device_id *, const char *, void *, void *);
    cl_kernel(CLAPI *CreateKernel)(cl_program, const char *, cl_int *);
    cl_int(CLAPI *SetKernelArg)(cl_kernel, cl_uint, size_t, const void *);
    cl_int(CLAPI *EnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t *, const size_t *,
                                        const size_t *, cl_uint, const cl_event *, cl_event *);
    cl_int(CLAPI *Finish)(cl_command_queue);
    cl_mem(CLAPI *CreateBuffer)(cl_context, cl_bitfield, size_t, void *, cl_int *);
    cl_int(CLAPI *EnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, const void *, cl_uint,
                                      const cl_event *, cl_event *);
    cl_int(CLAPI *ReleaseMemObject)(cl_mem);
    cl_int(CLAPI *ReleaseKernel)(cl_kernel);
    cl_int(CLAPI *ReleaseProgram)(cl_program);
    cl_int(CLAPI *ReleaseCommandQueue)(cl_command_queue);
    cl_int(CLAPI *ReleaseContext)(cl_context);
} cl;

#define CL_DEVICE_TYPE_GPU (1 << 2)
#define CL_DEVICE_TYPE_ACCELERATOR (1 << 3)
#define CL_DEVICE_MAX_COMPUTE_UNITS 0x1002
#define CL_DEVICE_MAX_CLOCK_FREQUENCY 0x100C
#define CL_DEVICE_MAX_MEM_ALLOC_SIZE 0x1010
#define CL_DEVICE_GLOBAL_MEM_SIZE 0x101F
#define CL_DEVICE_NAME 0x102B
#define CL_DEVICE_VENDOR 0x102C
#define CL_DRIVER_VERSION 0x102D
#define CL_CONTEXT_PLATFORM 0x1084
#define CL_MEM_READ_WRITE 1

static const char *cl_src =
    "__kernel void flops(__global float *out, float a, float b) {\n"
    "  float x0 = get_global_id(0) * 1e-7f, x1 = x0 + 0.1f, x2 = x0 + 0.2f, x3 = x0 + 0.3f;\n"
    "  float x4 = x0 + 0.4f, x5 = x0 + 0.5f, x6 = x0 + 0.6f, x7 = x0 + 0.7f;\n"
    "  for (int i = 0; i < 512; i++) {\n"
    "    x0 = mad(x0, a, b); x1 = mad(x1, a, b); x2 = mad(x2, a, b); x3 = mad(x3, a, b);\n"
    "    x4 = mad(x4, a, b); x5 = mad(x5, a, b); x6 = mad(x6, a, b); x7 = mad(x7, a, b);\n"
    "  }\n"
    "  out[get_global_id(0)] = x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7;\n"
    "}\n"
    "__kernel void copy(__global const float4 *a, __global float4 *b) {\n"
    "  size_t i = get_global_id(0); b[i] = a[i];\n"
    "}\n";
#define FLOPS_PER_ITEM (512.0 * 8 * 2)

static int cl_load(void)
{
    static const char *names[] = {"libOpenCL.so.1", "libOpenCL.so", "OpenCL.dll",
                                  "/System/Library/Frameworks/OpenCL.framework/OpenCL", NULL};
    void *lib = stx_dlopen(names);
    if (!lib)
        return 0;
#define L(f) if (!(*(void **)&cl.f = stx_dlsym(lib, "cl" #f))) return 0
    L(GetPlatformIDs); L(GetDeviceIDs); L(GetDeviceInfo); L(CreateContext); L(CreateCommandQueue);
    L(CreateProgramWithSource); L(BuildProgram); L(CreateKernel); L(SetKernelArg); L(EnqueueNDRangeKernel);
    L(Finish); L(CreateBuffer); L(EnqueueWriteBuffer); L(ReleaseMemObject); L(ReleaseKernel);
    L(ReleaseProgram); L(ReleaseCommandQueue); L(ReleaseContext);
#undef L
    return 1;
}

/* time `reps` launches of a 1D kernel */
static double cl_run(cl_command_queue q, cl_kernel k, size_t global, int reps)
{
    double t = stx_now();
    int i;
    for (i = 0; i < reps; i++)
        if (cl.EnqueueNDRangeKernel(q, k, 1, NULL, &global, NULL, 0, NULL, NULL) != 0)
            return -1;
    cl.Finish(q);
    return stx_now() - t;
}

static double cl_timed(cl_command_queue q, cl_kernel k, size_t global)
{
    int reps = 1;
    double t;
    if (cl_run(q, k, global, 1) < 0) /* warm-up, JIT */
        return -1;
    for (;;) {
        t = cl_run(q, k, global, reps);
        if (t < 0)
            return -1;
        if (t >= stx_cfg.unit_time || reps >= (1 << 20))
            break;
        reps *= t < stx_cfg.unit_time / 20 ? 8 : 2;
    }
    return t / reps;
}

static int opencl_bench(int idx, cl_platform_id plat, cl_device_id dev)
{
    char name[256] = "", vendor[128] = "", drv[64] = "", key[64];
    cl_uint cus = 0, mhz = 0;
    cl_ulong gmem = 0, maxalloc = 0;
    cl_context_properties props[3];
    cl_context ctx;
    cl_command_queue q;
    cl_program prog;
    cl_kernel kf, kc;
    cl_mem out, a, b;
    cl_int err;
    size_t global = 1 << 20, bytes;
    float fa = 0.999f, fb = 0.0001f;
    double t;

    cl.GetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(name), name, NULL);
    cl.GetDeviceInfo(dev, CL_DEVICE_VENDOR, sizeof(vendor), vendor, NULL);
    cl.GetDeviceInfo(dev, CL_DRIVER_VERSION, sizeof(drv), drv, NULL);
    cl.GetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cus), &cus, NULL);
    cl.GetDeviceInfo(dev, CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(mhz), &mhz, NULL);
    cl.GetDeviceInfo(dev, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(gmem), &gmem, NULL);
    cl.GetDeviceInfo(dev, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(maxalloc), &maxalloc, NULL);
    snprintf(key, sizeof(key), "opencl gpu%d", idx);
    stx_info(key, "%s (%s), %u CUs @ %u MHz, %.1f GiB, driver %s", name, vendor, cus, mhz, gmem / 1073741824.0, drv);

    props[0] = CL_CONTEXT_PLATFORM;
    props[1] = (cl_context_properties)plat;
    props[2] = 0;
    ctx = cl.CreateContext(props, 1, &dev, NULL, NULL, &err);
    if (!ctx || err)
        return 0;
    q = cl.CreateCommandQueue(ctx, dev, 0, &err);
    prog = cl.CreateProgramWithSource(ctx, 1, &cl_src, NULL, &err);
    if (!q || !prog || cl.BuildProgram(prog, 1, &dev, "", NULL, NULL) != 0) {
        stx_note("OpenCL program build failed on %s", name);
        return 0;
    }
    kf = cl.CreateKernel(prog, "flops", &err);
    kc = cl.CreateKernel(prog, "copy", &err);

    /* compute */
    stx_progress("gpu%d fp32 compute", idx);
    out = cl.CreateBuffer(ctx, CL_MEM_READ_WRITE, global * sizeof(float), NULL, &err);
    cl.SetKernelArg(kf, 0, sizeof(cl_mem), &out);
    cl.SetKernelArg(kf, 1, sizeof(float), &fa);
    cl.SetKernelArg(kf, 2, sizeof(float), &fb);
    t = cl_timed(q, kf, global);
    snprintf(key, sizeof(key), "gpu%d fp32 compute", idx);
    if (t > 0)
        stx_metric(key, global * FLOPS_PER_ITEM / t / 1e9, "GFLOPS", REF_GPU_FLOPS, STX_HIGHER);
    cl.ReleaseMemObject(out);

    /* device memory bandwidth */
    bytes = 256u << 20;
    if (maxalloc && bytes > maxalloc / 2)
        bytes = (size_t)(maxalloc / 2) & ~(size_t)0xffff;
    a = cl.CreateBuffer(ctx, CL_MEM_READ_WRITE, bytes, NULL, &err);
    b = cl.CreateBuffer(ctx, CL_MEM_READ_WRITE, bytes, NULL, &err);
    if (a && b) {
        void *host = stx_alloc(bytes, 4096);
        stx_progress("gpu%d memory bandwidth", idx);
        cl.SetKernelArg(kc, 0, sizeof(cl_mem), &a);
        cl.SetKernelArg(kc, 1, sizeof(cl_mem), &b);
        t = cl_timed(q, kc, bytes / 16);
        snprintf(key, sizeof(key), "gpu%d memory bandwidth", idx);
        if (t > 0)
            stx_metric(key, 2.0 * bytes / t / 1e9, "GB/s", REF_GPU_BW, STX_HIGHER);
        if (host) {
            int i, reps = 0;
            double t0;
            memset(host, 1, bytes);
            stx_progress("gpu%d host to device", idx);
            cl.EnqueueWriteBuffer(q, a, 1, 0, bytes, host, 0, NULL, NULL);
            t0 = stx_now();
            for (i = 0; i < 64 && stx_now() - t0 < stx_cfg.unit_time; i++, reps++)
                cl.EnqueueWriteBuffer(q, a, 1, 0, bytes, host, 0, NULL, NULL);
            t = stx_now() - t0;
            snprintf(key, sizeof(key), "gpu%d host->device (PCIe)", idx);
            if (t > 0 && reps)
                stx_metric(key, (double)bytes * reps / t / 1e9, "GB/s", REF_GPU_H2D, STX_HIGHER);
            stx_free(host);
        }
    }
    if (a)
        cl.ReleaseMemObject(a);
    if (b)
        cl.ReleaseMemObject(b);
    cl.ReleaseKernel(kf);
    cl.ReleaseKernel(kc);
    cl.ReleaseProgram(prog);
    cl.ReleaseCommandQueue(q);
    cl.ReleaseContext(ctx);
    return 1;
}

static int opencl_all(void)
{
    cl_platform_id plats[8];
    cl_uint np = 0, p;
    int n = 0;
    if (!cl_load())
        return -1;
    if (cl.GetPlatformIDs(8, plats, &np) != 0)
        return 0;
    if (np > 8)
        np = 8;
    for (p = 0; p < np; p++) {
        cl_device_id devs[8];
        cl_uint nd = 0, d;
        if (cl.GetDeviceIDs(plats[p], CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_ACCELERATOR, 8, devs, &nd) != 0)
            continue;
        if (nd > 8)
            nd = 8;
        for (d = 0; d < nd; d++)
            n += opencl_bench(n, plats[p], devs[d]);
    }
    return n;
}

/* ---- NVML (NVIDIA sensors) ---- */
typedef struct {
    unsigned long long total, free, used;
} nvml_mem;

static void nvml_report(void)
{
    static const char *names[] = {"libnvidia-ml.so.1", "libnvidia-ml.so", "nvml.dll", NULL};
    int(CLAPI * init)(void);
    int(CLAPI * count)(unsigned *);
    int(CLAPI * handle)(unsigned, void **);
    int(CLAPI * name)(void *, char *, unsigned);
    int(CLAPI * temp)(void *, int, unsigned *);
    int(CLAPI * power)(void *, unsigned *);
    int(CLAPI * clock)(void *, int, unsigned *);
    int(CLAPI * cur_clock)(void *, int, unsigned *);
    int(CLAPI * mem)(void *, nvml_mem *);
    int(CLAPI * gen)(void *, unsigned *);
    int(CLAPI * width)(void *, unsigned *);
    int(CLAPI * maxgen)(void *, unsigned *);
    int(CLAPI * maxwidth)(void *, unsigned *);
    int(CLAPI * shutdown)(void);
    void *lib = stx_dlopen(names);
    unsigned n = 0, i;
    if (!lib)
        return;
    *(void **)&init = stx_dlsym(lib, "nvmlInit_v2");
    *(void **)&count = stx_dlsym(lib, "nvmlDeviceGetCount_v2");
    *(void **)&handle = stx_dlsym(lib, "nvmlDeviceGetHandleByIndex_v2");
    *(void **)&name = stx_dlsym(lib, "nvmlDeviceGetName");
    *(void **)&temp = stx_dlsym(lib, "nvmlDeviceGetTemperature");
    *(void **)&power = stx_dlsym(lib, "nvmlDeviceGetPowerUsage");
    *(void **)&clock = stx_dlsym(lib, "nvmlDeviceGetMaxClockInfo");
    *(void **)&cur_clock = stx_dlsym(lib, "nvmlDeviceGetClockInfo");
    *(void **)&mem = stx_dlsym(lib, "nvmlDeviceGetMemoryInfo");
    *(void **)&gen = stx_dlsym(lib, "nvmlDeviceGetCurrPcieLinkGeneration");
    *(void **)&width = stx_dlsym(lib, "nvmlDeviceGetCurrPcieLinkWidth");
    *(void **)&maxgen = stx_dlsym(lib, "nvmlDeviceGetMaxPcieLinkGeneration");
    *(void **)&maxwidth = stx_dlsym(lib, "nvmlDeviceGetMaxPcieLinkWidth");
    *(void **)&shutdown = stx_dlsym(lib, "nvmlShutdown");
    if (!init || !count || !handle || init() != 0)
        return;
    count(&n);
    for (i = 0; i < n && i < 16; i++) {
        void *h;
        char nm[96] = "?", key[64], line[256];
        unsigned v, g = 0, w = 0, mg = 0, mw = 0;
        nvml_mem m;
        size_t l;
        if (handle(i, &h) != 0)
            continue;
        if (name)
            name(h, nm, sizeof(nm));
        l = (size_t)snprintf(line, sizeof(line), "%s", nm);
        if (mem && mem(h, &m) == 0)
            l += (size_t)snprintf(line + l, sizeof(line) - l, ", %.1f GiB", m.total / 1073741824.0);
        if (temp && temp(h, 0, &v) == 0)
            l += (size_t)snprintf(line + l, sizeof(line) - l, ", %u C", v);
        if (power && power(h, &v) == 0)
            l += (size_t)snprintf(line + l, sizeof(line) - l, ", %.1f W", v / 1000.0);
        if (clock && clock(h, 0, &v) == 0)
            l += (size_t)snprintf(line + l, sizeof(line) - l, ", max %u MHz", v);
        if (cur_clock && clock && cur_clock(h, 2, &v) == 0 && clock(h, 2, &g) == 0)
            l += (size_t)snprintf(line + l, sizeof(line) - l, ", mem clock %u/%u MHz", v, g);
        g = 0;
        snprintf(key, sizeof(key), "nvidia gpu%u", i);
        stx_info(key, "%s", line);
        if (gen && width && gen(h, &g) == 0 && width(h, &w) == 0) {
            snprintf(key, sizeof(key), "nvidia gpu%u pcie", i);
            if (maxgen && maxwidth && maxgen(h, &mg) == 0 && maxwidth(h, &mw) == 0)
                stx_info(key, "gen%u x%u (max gen%u x%u)", g, w, mg, mw);
            else
                stx_info(key, "gen%u x%u", g, w);
        }
    }
    if (shutdown)
        shutdown();
}

#ifdef STX_OS_LINUX
static const char *pci_vendor_name(unsigned v)
{
    switch (v) {
    case 0x10de: return "NVIDIA";
    case 0x1002: return "AMD";
    case 0x8086: return "Intel";
    case 0x1a03: return "ASPEED (BMC VGA)";
    case 0x102b: return "Matrox";
    case 0x15ad: return "VMware";
    case 0x1234: return "QEMU";
    case 0x1af4: return "virtio";
    case 0x5143: return "Qualcomm";
    case 0x13b5: return "ARM";
    case 0x1414: return "Microsoft";
    default: return "unknown vendor";
    }
}

static void drm_cb(const char *name, void *ctx)
{
    int *n = (int *)ctx;
    char p[512], drv[128] = "", line[256];
    uint64_t vendor = 0, device = 0, v;
    size_t l;
    if (strncmp(name, "card", 4) != 0 || strchr(name, '-'))
        return;
    snprintf(p, sizeof(p), "/sys/class/drm/%s/device/vendor", name);
    if (stx_read_u64(p, &vendor) != 0)
        return;
    snprintf(p, sizeof(p), "/sys/class/drm/%s/device/device", name);
    stx_read_u64(p, &device);
    snprintf(p, sizeof(p), "/sys/class/drm/%s/device/uevent", name);
    {
        char ue[1024], *d;
        if (stx_read_text(p, ue, sizeof(ue)) > 0 && (d = strstr(ue, "DRIVER=")) != NULL) {
            char *e = strchr(d, '\n');
            snprintf(drv, sizeof(drv), "%.*s", e ? (int)(e - d - 7) : (int)strlen(d + 7), d + 7);
        }
    }
    l = (size_t)snprintf(line, sizeof(line), "%s [%04llx:%04llx], driver %s", pci_vendor_name((unsigned)vendor),
                         (unsigned long long)vendor, (unsigned long long)device, drv[0] ? drv : "none");
    snprintf(p, sizeof(p), "/sys/class/drm/%s/device/mem_info_vram_total", name);
    if (stx_read_u64(p, &v) == 0)
        l += (size_t)snprintf(line + l, sizeof(line) - l, ", %.1f GiB VRAM", v / 1073741824.0);
    snprintf(p, sizeof(p), "/sys/class/drm/%s/device/gpu_busy_percent", name);
    if (stx_read_u64(p, &v) == 0)
        l += (size_t)snprintf(line + l, sizeof(line) - l, ", busy %llu %%", (unsigned long long)v);
    stx_info(name, "%s", line);
    (*n)++;
}
#endif

void stx_mod_gpu(void)
{
    int found = 0, n;
    stx_section("gpu", "GPU");
#ifdef STX_OS_LINUX
    stx_list_dir("/sys/class/drm", drm_cb, &found);
#endif
    nvml_report();
    n = opencl_all();
    if (n < 0) {
#ifdef STX_NO_DLOPEN
        stx_skip("static build: GPU compute needs the dynamic build (run.sh builds it when a compiler exists)");
#else
        stx_skip("no OpenCL runtime installed (comes with the GPU driver: nvidia, mesa rusticl/clover, rocm, intel)");
#endif
    } else if (n == 0) {
        stx_skip(found ? "no OpenCL-capable GPU driver for the detected GPU(s)" : "no GPU found");
    }
}
