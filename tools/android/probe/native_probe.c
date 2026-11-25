#include <dlfcn.h>
#include <errno.h>
#include <jni.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "libtcc.h"

static const char source[] =
    "int probe_entry(int (*helper)(int), int value) { return helper(value * 7 + 3); }";

/* Serializes activity recreation with the one build and its atomic cache publish. */
static pthread_mutex_t probe_lock = PTHREAD_MUTEX_INITIALIZER;

static double elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

static long peak_rss_kib(void) {
    struct rusage usage;
    return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss : -1;
}

static void compiler_error(void *opaque, const char *message) {
    snprintf(opaque, 512, "%s", message);
}

static int compile_module(const char *path, char error[512]) {
    TCCState *compiler = tcc_new();
    if (!compiler) {
        snprintf(error, 512, "tcc_new failed");
        return -1;
    }
    tcc_set_error_func(compiler, error, compiler_error);
    int result = tcc_set_options(compiler, "-nostdlib -nostdinc -Wl,-section-alignment=4000");
    if (result == 0)
        result = tcc_set_output_type(compiler, TCC_OUTPUT_DLL);
    if (result == 0)
        result = tcc_compile_string(compiler, source);
    if (result == 0)
        result = tcc_output_file(compiler, path);
    tcc_delete(compiler);
    return result;
}

static int runtime_helper(int input) {
    return input + 11;
}

static void run_probe(const char *directory, char report[2048]) {
    char module[PATH_MAX];
    char pending[PATH_MAX];
    if (snprintf(module, sizeof(module), "%s/probe-v1.so", directory) >= (int)sizeof(module)
        || snprintf(pending, sizeof(pending), "%s/probe-v1.pending", directory) >= (int)sizeof(pending)) {
        snprintf(report, 2048, "FAIL — module path too long\n");
        return;
    }

    struct stat info;
    char build[256] = "Reused cached ELF. Compiler was not run.";
    char outcome[768];
    int passed = 0;
    if (stat(module, &info) != 0) {
        if (errno != ENOENT) {
            snprintf(report, 2048, "FAIL — cache stat: %s\n", strerror(errno));
            return;
        }
        const long before = peak_rss_kib();
        struct timespec start, end;
        char error[512] = "compiler returned an error";
        clock_gettime(CLOCK_MONOTONIC, &start);
        int result = compile_module(pending, error);
        clock_gettime(CLOCK_MONOTONIC, &end);
        snprintf(build, sizeof(build),
            "%s on this launch in %.3f ms (compile, link and write).\n"
            "Process peak RSS before/after compile: %ld / %ld KiB.",
            result == 0 ? "Compiled" : "Compile failed", elapsed_ms(start, end), before, peak_rss_kib());
        if (result != 0 || chmod(pending, S_IRUSR) != 0 || rename(pending, module) != 0) {
            snprintf(outcome, sizeof(outcome), "%s: %s", result != 0 ? "TinyCC" : "Publish",
                result != 0 ? error : strerror(errno));
            unlink(pending);
            goto done;
        }
    }

    if (stat(module, &info) != 0 || (info.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0) {
        snprintf(outcome, sizeof(outcome), "Expected a read-only ELF in internal app storage");
        goto done;
    }
    void *handle = dlopen(module, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        snprintf(outcome, sizeof(outcome), "dlopen: %s", dlerror());
        goto done;
    }
    typedef int (*entry_func)(int (*)(int), int);
    entry_func entry = (entry_func)dlsym(handle, "probe_entry");
    if (entry == NULL) {
        snprintf(outcome, sizeof(outcome), "dlsym: %s", dlerror());
    } else {
        int actual = entry(runtime_helper, 5);
        passed = actual == 49;
        snprintf(outcome, sizeof(outcome), "Loaded ELF and called packaged helper. Result: %d (expected 49).", actual);
    }
    dlclose(handle);

done:
    if (stat(module, &info) != 0)
        memset(&info, 0, sizeof(info));
    snprintf(report, 2048, "%s — ARM64 native module\n\n%s\n%s\n\n"
        "Process peak RSS: %ld KiB (lifetime high-water mark).\n"
        "Private module: %lld bytes logical / %lld bytes allocated.\n"
        "Build uses one temporary module file, renamed into place.",
        passed ? "PASS" : "FAIL", build, outcome, peak_rss_kib(),
        (long long)info.st_size, (long long)info.st_blocks * 512);
}

JNIEXPORT jstring JNICALL
Java_net_srnative_sonicriders_probe_NativeProbeActivity_runProbe(JNIEnv *env, jclass activity,
    jstring directory) {
    (void)activity;
    const char *path = (*env)->GetStringUTFChars(env, directory, NULL);
    if (!path)
        return NULL;
    char report[2048];
    pthread_mutex_lock(&probe_lock);
    run_probe(path, report);
    pthread_mutex_unlock(&probe_lock);
    (*env)->ReleaseStringUTFChars(env, directory, path);
    return (*env)->NewStringUTF(env, report);
}
