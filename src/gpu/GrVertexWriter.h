/*
 * Copyright 2010 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrVertexWriter_DEFINED
#define GrVertexWriter_DEFINED

#include "include/private/SkTemplates.h"
#include "src/gpu/GrColor.h"
#include "src/gpu/geometry/GrQuad.h"
#include <type_traits>

/**
 * Helper for writing vertex data to a buffer. Usage:
 *  GrVertexWriter vertices{target->makeVertexSpace(...)};
 *  vertices.write(A0, B0, C0, ...);
 *  vertices.write(A1, B1, C1, ...);
 *
 * Supports any number of arguments. Each argument must be POD (plain old data), or an array
 * thereof.
 */
struct GrVertexWriter {
    inline constexpr static uint32_t kIEEE_32_infinity = 0x7f800000;

    void* fPtr;

    GrVertexWriter() = default;
    GrVertexWriter(void* ptr) : fPtr(ptr) {}
    GrVertexWriter(const GrVertexWriter&) = delete;
    GrVertexWriter(GrVertexWriter&& that) { *this = std::move(that); }

    GrVertexWriter& operator=(const GrVertexWriter&) = delete;
    GrVertexWriter& operator=(GrVertexWriter&& that) {
        fPtr = that.fPtr;
        that.fPtr = nullptr;
        return *this;
    }

    bool operator==(const GrVertexWriter& that) const { return fPtr == that.fPtr; }
    operator bool() const { return fPtr != nullptr; }

    GrVertexWriter makeOffset(ptrdiff_t offsetInBytes) const {
        return {SkTAddOffset<void>(fPtr, offsetInBytes)};
    }

    template <typename T>
    class Conditional {
    public:
        explicit Conditional(bool condition, const T& value)
            : fCondition(condition), fValue(value) {}
    private:
        friend struct GrVertexWriter;

        bool fCondition;
        T fValue;
    };

    template <typename T>
    static Conditional<T> If(bool condition, const T& value) {
        return Conditional<T>(condition, value);
    }

    template <typename T>
    struct Skip {};

    template <typename T, typename... Args>
    void write(const T& val, const Args&... remainder) {
        static_assert(std::is_pod<T>::value, "");
        memcpy(fPtr, &val, sizeof(T));
        fPtr = SkTAddOffset<void>(fPtr, sizeof(T));
        this->write(remainder...);
    }

    template <typename T, size_t N, typename... Args>
    void write(const T(&val)[N], const Args&... remainder) {
        static_assert(std::is_pod<T>::value, "");
        memcpy(fPtr, val, N * sizeof(T));
        fPtr = SkTAddOffset<void>(fPtr, N * sizeof(T));
        this->write(remainder...);
    }

    template <typename... Args>
    void write(const GrVertexColor& color, const Args&... remainder) {
        this->write(color.fColor[0]);
        if (color.fWideColor) {
            this->write(color.fColor[1]);
            this->write(color.fColor[2]);
            this->write(color.fColor[3]);
        }
        this->write(remainder...);
    }

    template <typename T, typename... Args>
    void write(const Conditional<T>& val, const Args&... remainder) {
        if (val.fCondition) {
            this->write(val.fValue);
        }
        this->write(remainder...);
    }

    template <typename T, typename... Args>
    void write(const Skip<T>& val, const Args&... remainder) {
        fPtr = SkTAddOffset<void>(fPtr, sizeof(T));
        this->write(remainder...);
    }

    template <typename... Args>
    void write(const Sk4f& vector, const Args&... remainder) {
        float buffer[4];
        vector.store(buffer);
        this->write<float, 4>(buffer);
        this->write(remainder...);
    }

    template <typename T>
    void writeArray(const T* array, int count) {
        static_assert(std::is_pod<T>::value, "");
        memcpy(fPtr, array, count * sizeof(T));
        fPtr = SkTAddOffset<void>(fPtr, count * sizeof(T));
    }

    template <typename T>
    void fill(const T& val, int repeatCount) {
        for (int i = 0; i < repeatCount; ++i) {
            this->write(val);
        }
    }

    void writeRaw(const void* data, size_t size) {
        memcpy(fPtr, data, size);
        fPtr = SkTAddOffset<void>(fPtr, size);
    }

    void write() {}

    /**
     * Specialized utility for writing a four-vertices, with some data being replicated at each
     * vertex, and other data being the appropriate 2-components from an SkRect to construct a
     * triangle strip.
     *
     * writeQuad(A, B, C, ...) is similar to write(A, B, C, ...), except that:
     *
     * - Four sets of data will be written
     * - For any arguments of type TriStrip, a unique SkPoint will be written at each vertex,
     *   in this order: left-top, left-bottom, right-top, right-bottom.
     */
    template <typename T>
    struct TriStrip { T l, t, r, b; };

    static TriStrip<float> TriStripFromRect(const SkRect& r) {
        return { r.fLeft, r.fTop, r.fRight, r.fBottom };
    }

    static TriStrip<uint16_t> TriStripFromUVs(const std::array<uint16_t, 4>& rect) {
        return { rect[0], rect[1], rect[2], rect[3] };
    }

    template <typename T>
    struct TriFan { T l, t, r, b; };

    static TriFan<float> TriFanFromRect(const SkRect& r) {
        return { r.fLeft, r.fTop, r.fRight, r.fBottom };
    }

    template <typename... Args>
    void writeQuad(const Args&... remainder) {
        this->writeQuadVert<0>(remainder...);
        this->writeQuadVert<1>(remainder...);
        this->writeQuadVert<2>(remainder...);
        this->writeQuadVert<3>(remainder...);
    }

private:
    template <int corner, typename T, typename... Args>
    void writeQuadVert(const T& val, const Args&... remainder) {
        this->writeQuadValue<corner>(val);
        this->writeQuadVert<corner>(remainder...);
    }

    template <int corner>
    void writeQuadVert() {}

    template <int corner, typename T>
    void writeQuadValue(const T& val) {
        this->write(val);
    }

    template <int corner, typename T>
    void writeQuadValue(const TriStrip<T>& r) {
        switch (corner) {
            case 0: this->write(r.l, r.t); break;
            case 1: this->write(r.l, r.b); break;
            case 2: this->write(r.r, r.t); break;
            case 3: this->write(r.r, r.b); break;
        }
    }

    template <int corner, typename T>
    void writeQuadValue(const TriFan<T>& r) {
        switch (corner) {
        case 0: this->write(r.l, r.t); break;
        case 1: this->write(r.l, r.b); break;
        case 2: this->write(r.r, r.b); break;
        case 3: this->write(r.r, r.t); break;
        }
    }

    template <int corner>
    void writeQuadValue(const GrQuad& q) {
        this->write(q.point(corner));
    }
};

#endif
