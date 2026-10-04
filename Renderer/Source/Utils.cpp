#include "utils.hpp"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

utils::FileData utils::file_read(const char* path) {
    DEBUG_ASSERT(path);

    FileData result{};

    FILE* const fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "%s: fopen %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    DEFER(fclose(fp));

    if (fseek(fp, 0, SEEK_END)) {
        fprintf(stderr, "%s: fseek SEEK_END %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    const long file_size = ftell(fp);
    if (file_size == -1) {
        fprintf(stderr, "%s: ftell %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    if (fseek(fp, 0, SEEK_SET)) {
        fprintf(stderr, "%s: fseek SEEK_SET %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }

    void* const res = malloc(size_t((file_size + 1)) * sizeof(u8));
    if (!res) {
        fprintf(stderr, "%s: malloc failed (size %ld): %s\n", __func__, file_size, strerror(errno));
        return result;
    }

    if (fread(res, sizeof(u8), size_t(file_size), fp) != size_t(file_size)) {
        if (feof(fp)) {
            fprintf(stderr, "%s: fread %s failed: EOF\n", __func__, path);
        } else if (ferror(fp)) {
            fprintf(stderr, "%s: fread %s failed: %s\n", __func__, path, strerror(errno));
        }
        free(res);
        return result;
    }

    result.data = res;
    result.size = file_size;

    return result;
}

void* utils::xmalloc(size_t size) {
    void* const ret = malloc(size);
    if (!ret) {
        fprintf(stderr, "malloc failed (size = %zu)\n", size);
        exit(1);
    }
    return ret;
}

void* utils::xrealloc(void* ptr, size_t new_size) {
    void* const ret = realloc(ptr, new_size);
    if (!ret) {
        fprintf(stderr, "realloc failed (new_size = %zu)\n", new_size);
        exit(1);
    }
    return ret;
}

/*
 * Copy string src to buffer dst of size dsize.  At most dsize-1
 * chars will be copied.  Always NUL terminates (unless dsize == 0).
 * Returns strlen(src); if retval >= dsize, truncation occurred.
 */
size_t utils::strlcpy(char* dst, const char* src, size_t dsize) {
    const char* osrc = src;
    size_t nleft = dsize;

    /* Copy as many bytes as will fit. */
    if (nleft != 0) {
        while (--nleft != 0) {
            if ((*dst++ = *src++) == '\0')
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src. */
    if (nleft == 0) {
        if (dsize != 0)
            *dst = '\0'; /* NUL-terminate dst */
        while (*src++)
            ;
    }

    return size_t(src - osrc - 1); /* count does not include NUL */
}

/*
 * Appends src to string dst of size dsize (unlike strncat, dsize is the
 * full size of dst, not space left).  At most dsize-1 characters
 * will be copied.  Always NUL terminates (unless dsize <= strlen(dst)).
 * Returns strlen(src) + MIN(dsize, strlen(initial dst)).
 * If retval >= dsize, truncation occurred.
 */
size_t utils::strlcat(char* dst, const char* src, size_t dsize) {
    const char* odst = dst;
    const char* osrc = src;
    size_t n = dsize;
    size_t dlen;

    /* Find the end of dst and adjust bytes left but don't go past end. */
    while (n-- != 0 && *dst != '\0')
        dst++;
    dlen = size_t(dst - odst);
    n = dsize - dlen;

    if (n-- == 0)
        return (dlen + strlen(src));
    while (*src != '\0') {
        if (n != 0) {
            *dst++ = *src;
            n--;
        }
        src++;
    }
    *dst = '\0';

    return (dlen + size_t(src - osrc)); /* count does not include NUL */
}

void utils::FpsCounter::update(f64& fps, f64 time) {
    const f64 elapsed_time = time - m_prev_time;

    if (elapsed_time > 0.25) {
        m_prev_time = time;
        fps = f64(m_frame_count) / elapsed_time;
        m_frame_count = 0;
    }

    ++m_frame_count;
}

void utils::MemoryDivider::init(void* memory, ptrdiff_t size) {
    m_memory = memory;
    m_size = size;
    m_current_offset = 0;
}

MemorySlice utils::MemoryDivider::take(ptrdiff_t bytes) {
    DEBUG_ASSERT(m_size > 0);
    DEBUG_ASSERT(m_memory);
    DEBUG_ASSERT(m_current_offset < m_size);

    MemorySlice result{};

    const ptrdiff_t available = m_size - m_current_offset;
    if (bytes <= available) {
        result.count = bytes;
        result.data = static_cast<uchar*>(m_memory) + m_current_offset;
        m_current_offset += bytes + 1;
    }

    return result;
}

MemorySlice utils::MemoryDivider::take_rest() {
    DEBUG_ASSERT(m_size > 0);
    DEBUG_ASSERT(m_memory);
    DEBUG_ASSERT(m_current_offset < m_size);

    MemorySlice result{};
    result.count = m_size - m_current_offset - 1;
    result.data = static_cast<uchar*>(m_memory) + m_current_offset;
    m_current_offset += result.count + 1;
    DEBUG_ASSERT(m_current_offset == m_size);

    return result;
}

f32 utils::linear_to_srgb(f32 color, f32 gamma) {
    DEBUG_ASSERT(gamma > 0.0f);
    return powf(color, 1.0f / gamma);
}

f32 utils::srgb_to_linear(f32 color, f32 gamma) {
    DEBUG_ASSERT(gamma > 0.0f);
    return powf(color, gamma);
}

Vec3 utils::linear_to_srgb(Vec3 color, f32 gamma) {
    DEBUG_ASSERT(gamma > 0.0f);
    f32 inverseGamma = 1.0f / gamma;
    return {
        powf(color.val[0], inverseGamma),
        powf(color.val[1], inverseGamma),
        powf(color.val[2], inverseGamma)
    };
}

Vec3 utils::srgb_to_linear(Vec3 color, f32 gamma) {
    DEBUG_ASSERT(gamma > 0.0f);
    return {powf(color.val[0], gamma), powf(color.val[1], gamma), powf(color.val[2], gamma)};
}
