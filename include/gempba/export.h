/*
 * MIT License
 *
 * Copyright (c) 2026. Andrés Pastrana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef GEMPBA_EXPORT_H
#define GEMPBA_EXPORT_H

/**
 * @file
 * @brief Marks what libgempba (@c GEMPBA_API) and libgempba_mpi (@c GEMPBA_MPI_API) export; the rest of each library is hidden.
 *
 * On Windows a consumer needs no @c dllimport: MinGW's linker resolves the calls through each DLL's import library, and the
 * tests and the JNI library can link the same objects without the DLLs.
 */

#if defined(_WIN32)
    #if defined(GEMPBA_BUILDING_CORE)
        #define GEMPBA_API __declspec(dllexport)
    #else
        #define GEMPBA_API
    #endif
    #if defined(GEMPBA_BUILDING_MPI)
        #define GEMPBA_MPI_API __declspec(dllexport)
    #else
        #define GEMPBA_MPI_API
    #endif
#else
    #define GEMPBA_API __attribute__((visibility("default")))
    #define GEMPBA_MPI_API __attribute__((visibility("default")))
#endif

#endif // GEMPBA_EXPORT_H
